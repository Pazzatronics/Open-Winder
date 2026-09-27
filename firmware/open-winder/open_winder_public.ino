/*
  Open Winder - Guitar Pickup Coil Winder Firmware
  Teensy 4.1

  Public release (derived from rev 2026-09-16b)
  ---------------------------------------------
  - Product-specific winding presets removed. Set up a job with
    dir / rpm / turns / space (and optional taps), then cal or defcal, then start.
  - The defcal window (DEFAULT_PROFILE_* and DEFAULT_CAL_OFF* constants below) came
    from the reference machine. For your machine: run cal, jog + savet / saveb,
    donecal, then stat, and copy DEF_TOP_INSET_STEPS / DEF_BOTTOM_INSET_STEPS into
    DEFAULT_PROFILE_TOP_INSET_STEPS / DEFAULT_PROFILE_BOTTOM_INSET_STEPS.
  - Set TRAV_LEAD_UM_PER_REV and both *_STEPS_PER_REV to match your hardware and
    driver DIP switches before the first run.
  - See the accompanying wiring CSV for pin-to-terminal connections.

  Revision 2026-09-16b (August audit fixes ported onto the proven 09-16 build)
  ---------------------------------------------------------------------------
  - Stop outranks pause; stale stop/pause requests are cleared on resume, so a stop
    sent during a pause decel no longer terminates the run at the next tap.
  - Resume re-arms a pending auto recal instead of leaving it stuck until the next stop.
  - Taps and the total-turn stop anticipate the ramp-down (rpm^2 / (120*ramp) turns) so
    the wind lands ON the target instead of ~28 turns past it at 500 rpm / 75 rpm/s.
  - Only one tap can be pending at a time (second tap during one decel no longer
    overwrites the first).
  - Chained faults (a second fault while already faulted) no longer overwrite the saved
    resume context.
  - ack restarts a calibration that a fault interrupted (auto recal, or a defcal/cal from
    a paused run) so the saved run can still be restored instead of stranding it.
  - Manual cal started from PAUSED_AUTO/TAP_PAUSE returns to the paused run after donecal
    (traverse position restored) instead of stranding it in READY.
  - CAL_JOG: jogging into a limit switch stops the traverse.
  - nudget/nudgeb abort on permit/snag (when those inputs are fitted).
  - zero/tare rejected while the machine is moving (they block for up to 3 s).
  - spin ramps to speed instead of commanding full step rate in one jump (and loop() no
    longer re-applies full RPM every pass, which was defeating the ramp).
  - StepperAxis: in-flight step counted on stopImmediate (was dropped: 10 um per hard
    stop), step direction latched at the rising edge so a late DIR change cannot miscount,
    position counts in LOGICAL direction so a DIR_INVERT flip cannot desync cal vs wind,
    begin() zeroes positionSteps.
  - Tension baseline: decay can no longer truncate to zero (slow tension drift no longer
    trips a false FAULT_TENSION) and the rise is slew-limited so one spike cannot inflate
    the fault threshold.
  - HX711 bit-bang pulses shortened to 500 ns (halves the per-sample blocking time).
  - Optional hardware watchdog (USE_WATCHDOG, off by default).
  - Fault/status lines label tension as raw counts until zero+tare are done.

  Revision 2026-09-16 (bench-verified hardware + idle-freeze fix)
  ---------------------------------------------------------------
  - WINDER_STEPS_PER_REV 1600 -> 400. Bench: 1600 pulses = 4.000 rev, 400 = 1 rev.
    Winder DM542: SW5 OFF, SW6 ON, SW7 ON, SW8 ON (400 steps/rev). No DIP change needed.
  - TRAV_STEPS_PER_REV stays 400 and REQUIRES the traverse DM542 at 400 steps/rev:
    SW5 OFF, SW6 ON, SW7 ON, SW8 ON. Bench found it at 1600 (SW6 OFF) -> flip SW6 ON.
    A static_assert now refuses to build if the step size is not a whole number of microns.
  - HX711 moved to pins 30 (DT) / 31 (SCK).
  - Run permit / snag fault compiled out (HAS_RUN_PERMIT / HAS_SNAG_FAULT = 0). Code kept.
  - Idle-freeze fix: StepperAxis re-seeds nextStepUs/pulseEndUs when an axis wakes from
    stopped, and update() resyncs a stale timestamp. Cause of the old bug: micros() wraps
    every 71.6 min; the signed (now - nextStepUs) compare went negative ~35.8 min after a
    stop, so resume/pause "worked" but no pulses came out until resetall re-seeded it.
  - Traverse reversal: DIR gets TRAV_REVERSAL_DIR_SETTLE_US (60 us) only when the direction
    actually changes; normal steps keep the 5 us setup. Prime suspect for the upward drift.
  - RECAL_DRIFT line at every auto recal (and defcal/cal from a paused run): counter error
    at each hard switch since the previous cal, plus a ready-to-type driftcomp suggestion.
  - driftcomp <steps> <turns>: gradual counter correction, <steps> spread evenly over every
    <turns> turns. Positive = the wind drifts toward TOP. driftcompoff disables.
  - reset: same as resetall but keeps the HX711 zero + tare calibration.
  - STATUS line adds act_top / act_bot (active bounds) so drift can be assessed from logs.
  - defcal banner is printed from the live constants (it used to hardcode stale numbers).
  - DEFAULT_SPACE_UM comment corrected: 1000 um = 1.000 mm (value unchanged).

  Earlier notes (retained)
  ------------------------
  4 mm/rev traverse screw update:
  - Traverse screw changed from 2.000 mm/rev to 4.000 mm/rev
  - Traverse step size changed from 0.005 mm/step to 0.010 mm/step
  - Calibration/jog direct traverse RPMs were halved to preserve the old linear travel speed
  - Nudge step delay was doubled to preserve the old nudge linear speed
  - Winding traverse spacing math remains in micrometers, so spacing commands stay in mm

  Turn-count fix applied:
  - loop() now captures winder.update(nowUs) return value
  - handleWinderStepEvents() counts one commanded step per event
  - no dependency on winder.positionSteps delta for turn counting

  Entropy update:
  - true entropy is gathered only at setup(), start, resume, and calibration start
  - no periodic reseeding while running
  - chaos RNG state is then held constant until the next setup/start/resume/calibration

  Calibrated tension update:
  - HX711 zero is captured only by the zero command with no reference mass applied
  - tare <grams> is captured only when the known reference mass is applied
  - reference mass is treated as negative force because it pulls opposite wire tension
  - wire tension is reported positive in calibrated grams-force
  - plotter defaults on and prints Min1s_g, Max1s_g, Avg3s_g

  Tension-drop fault update:
  - Fault recovery rule: any fault that interrupts an active/paused auto wind clears to PAUSED_AUTO with ack, so resume continues the same run
  - While RUNNING_AUTO, a learned tension baseline is tracked in calibrated grams-force
  - If force drops sharply below the adaptive threshold and stays low for 20 turns, machine enters FAULT_TENSION
  - This avoids a fixed low-grams cutoff so lighter winds are less likely to false-trigger
*/

#include <Arduino.h>
#include <Entropy.h>
#include <limits.h>

// -----------------------------------------------------------------------------
// Optional safety inputs
// -----------------------------------------------------------------------------
// Neither is wired on the reference machine: the power-supply switch is the E-stop.
// With a flag at 0 the pin is never read. (An unwired INPUT_PULLUP pin reads HIGH,
// which the fault logic treats as tripped, and the machine would sit in
// FAULT_FAILSAFE / FAULT_SNAG from boot.) Set to 1 and wire the pin to use it.
#define HAS_RUN_PERMIT 0
#define HAS_SNAG_FAULT 0

// Hardware watchdog (Teensy 4.1 core, Watchdog_t4.h - no extra library). If loop() ever
// wedges for more than WDT_TIMEOUT_S the chip reboots, which drops all step pulses.
// Off by default; set to 1 to enable. Blocking paths (tare, nudge) feed it themselves.
// NOTE: no function definitions may appear above the enum/struct declarations
// below. The Arduino IDE injects auto-generated prototypes at the first function
// it finds, and those prototypes reference MachineState/TraverseDir.
#define USE_WATCHDOG 0
#if USE_WATCHDOG
#include "Watchdog_t4.h"
static constexpr uint32_t WDT_TIMEOUT_S = 10;
WDT_T4<WDT1> wdt;
#endif

// -----------------------------------------------------------------------------
// Pins
// -----------------------------------------------------------------------------
static constexpr uint8_t PIN_WINDER_STEP   = 2;
static constexpr uint8_t PIN_WINDER_DIR    = 3;
static constexpr uint8_t PIN_TRAV_STEP     = 4;
static constexpr uint8_t PIN_TRAV_DIR      = 5;

static constexpr uint8_t PIN_BOTTOM_LIMIT  = 7;   // right / bottom  (bench-verified)
static constexpr uint8_t PIN_TOP_LIMIT     = 8;   // left / top      (bench-verified)

static constexpr uint8_t PIN_RUN_PERMIT    = 33;  // LOW = enabled   (only read if HAS_RUN_PERMIT)
static constexpr uint8_t PIN_SNAG_FAULT    = 34;  // LOW = OK        (only read if HAS_SNAG_FAULT; use 32 if wired later)

// HX711 tension/load-cell reader
// Uses ordinary digital GPIO, not UART/SPI/I2C. HX711 VCC must be 3.3 V.
static constexpr uint8_t PIN_HX711_DT      = 30;  // HX711 DT/DOUT   (was 22)
static constexpr uint8_t PIN_HX711_SCK     = 31;  // HX711 SCK/CLK   (was 23)

static constexpr bool WINDER_DIR_INVERT   = false;  // HIGH = CW (bench-verified)
static constexpr bool TRAVERSE_DIR_INVERT = false;  // HIGH = toward TOP (bench-verified)

// -----------------------------------------------------------------------------
// Timing
// -----------------------------------------------------------------------------
static constexpr uint32_t SERIAL_BAUD        = 115200;
static constexpr uint32_t STEP_PULSE_US      = 5;
static constexpr uint32_t DIR_SETUP_US       = 5;
static constexpr uint32_t STATUS_INTERVAL_MS = 1000;
static constexpr uint32_t LIMIT_DEBOUNCE_US  = 5000;  // reject EMI/bounce spikes during motion

// Extra DIR settle used only when the traverse actually reverses direction while winding.
// The driver's 5 us minimum assumes 4-5 V drive; the Teensy gives 3.3 V and the optocoupler
// turns off slower than it turns on. Paid once per stroke, so it costs nothing at speed.
static constexpr uint32_t TRAV_REVERSAL_DIR_SETTLE_US = 60;

// Tension logging / plotting
// HX711 sample rate is set by the board RATE pin/jumper. This code reads only when ready.
// Serial Plotter line is emitted once per second as a max-over-window value.
static constexpr uint32_t TENSION_WINDOW_MS = 1000;
static constexpr uint32_t TENSION_STALE_MS  = 2000;
static constexpr uint16_t TENSION_CAL_SAMPLES = 16;
static constexpr uint32_t HX711_PULSE_NS = 500;  // >= 200 ns per HX711 datasheet; halves blocking vs 1 us pulses

// Tension-drop fault detection
// Fault only runs during RUNNING_AUTO and only after zero + tare calibration.
// It uses a learned baseline so low-tension winds are not judged by the same fixed cutoff as tight winds.
static constexpr uint32_t TENSION_DROP_CONFIRM_TURNS = 20;
static constexpr long     TENSION_DROP_BASELINE_MIN_G = 8;      // do not arm until normal running tension has reached this
static constexpr uint8_t  TENSION_DROP_KEEP_PCT = 55;           // fault candidate if force <= baseline * 55%
static constexpr uint32_t TENSION_DROP_IGNORE_START_TURNS = 5;  // ignore first few turns after start/resume

// -----------------------------------------------------------------------------
// Mechanics
// -----------------------------------------------------------------------------
// Traverse DM542 must be set to 400 steps/rev: SW5 OFF, SW6 ON, SW7 ON, SW8 ON.
static constexpr int32_t TRAV_STEPS_PER_REV   = 400;
static constexpr int32_t TRAV_LEAD_UM_PER_REV = 4000;  // 4.000 mm / rev lead screw
static constexpr int32_t TRAV_STEP_UM         = TRAV_LEAD_UM_PER_REV / TRAV_STEPS_PER_REV;  // 10 um = 0.010 mm / step
static_assert(TRAV_LEAD_UM_PER_REV % TRAV_STEPS_PER_REV == 0,
              "TRAV_STEP_UM must be a whole number of microns (1600 steps/rev would give 2.5 um and silently truncate)");
// Winder DM542 is set to 400 steps/rev: SW5 OFF, SW6 ON, SW7 ON, SW8 ON (bench-verified 2026-09-16).
static constexpr int32_t WINDER_STEPS_PER_REV = 400;

// -----------------------------------------------------------------------------
// Defaults
// -----------------------------------------------------------------------------
static constexpr uint32_t MIN_RPM = 1;
static constexpr uint32_t MAX_RPM = 1500;

static constexpr uint32_t NUDGE_STEP_DELAY_US = 6000;  // doubled for 4 mm/rev screw; same linear nudge speed as old 2 mm/rev
static constexpr uint32_t NUDGE_DIR_SETTLE_US = 50;

static constexpr uint32_t CAL_FAST_RPM   = 60;   // was 120 with 2 mm/rev; same linear speed with 4 mm/rev
static constexpr uint32_t CAL_SLOW_RPM   = 10;   // was 20 with 2 mm/rev; same linear speed with 4 mm/rev
static constexpr uint32_t CAL_CENTER_RPM = 20;   // was 40 with 2 mm/rev; same linear speed with 4 mm/rev
static constexpr uint32_t JOG_RPM        = 12;   // approx old 25 rpm linear jog speed

static constexpr uint32_t DEFAULT_RPM     = 500;
static constexpr uint32_t START_FLOOR_RPM = 30;
static constexpr uint32_t DEFAULT_RAMP    = 75;
static constexpr uint32_t MIN_RAMP_RPM_S  = 5;
static constexpr float    STOP_RPM_EPS    = 0.50f;

static constexpr int32_t DEFAULT_SPACE_UM    = 1000;  // 1.000 mm per turn (confirmed)
static constexpr int32_t DEFAULT_SOFTDIST_UM = 500;   // 0.500 mm
static constexpr uint8_t DEFAULT_SOFTPCT     = 40;

static constexpr uint8_t  DEFAULT_CHAOS_PCT           = 19;
static constexpr bool     DEFAULT_CHAOS_ENABLE        = true;
static constexpr uint16_t DEFAULT_CHAOS_PERSIST_TURNS = 17;

static constexpr int32_t  CAL_BACKOFF_UM = 5500;
static constexpr uint32_t AUTO_RECAL_INTERVAL_TURNS = 2000;

// -----------------------------------------------------------------------------
// Default calibration profile (values from the reference machine - replace with
// the DEF_*_INSET_STEPS that stat prints after a manual cal on your machine)
//
// Meaning:
//   Find hard limits first, then rebuild working window relative to those limits.
//   TOP soft/work bound    = hardTop    - topInsetSteps
//   BOTTOM soft/work bound = hardBottom + bottomInsetSteps
//   The defcal banner is printed from these constants, so it always matches.
// -----------------------------------------------------------------------------
static constexpr int32_t DEFAULT_PROFILE_TOP_INSET_STEPS    = 1621;
static constexpr int32_t DEFAULT_PROFILE_BOTTOM_INSET_STEPS = 214;
static constexpr int32_t DEFAULT_CAL_OFFTOP_UM              = 3250;
static constexpr int32_t DEFAULT_CAL_OFFBOTTOM_UM           = 0;
static constexpr int32_t DEFAULT_PROFILE_SOFTDIST_UM        = 500;
static constexpr uint8_t DEFAULT_PROFILE_SOFTPCT            = 40;
static constexpr bool    DEFAULT_PROFILE_CHAOS_ENABLE       = true;
static constexpr uint8_t DEFAULT_PROFILE_CHAOS_PCT          = DEFAULT_CHAOS_PCT;

// -----------------------------------------------------------------------------
// Enums
// -----------------------------------------------------------------------------
enum class MachineState : uint8_t {
  IDLE = 0,
  CAL_FIND_LIMITS,
  CAL_JOG,
  READY,
  RUNNING_AUTO,
  RUNNING_SPIN,
  PAUSED_AUTO,
  TAP_PAUSE,
  FAULT_FAILSAFE,
  FAULT_SNAG,
  FAULT_TENSION,
  TERMINATED
};

enum class TraverseDir : int8_t {
  TO_BOTTOM = -1,
  TO_TOP    = 1
};

enum class CalMode : uint8_t {
  MANUAL = 0,
  PROFILE_TEMPLATE,
  AUTO_RECAL
};

enum class CalFindSubstate : uint8_t {
  GO_BOTTOM_FAST = 0,
  BACKOFF_BOTTOM,
  GO_BOTTOM_SLOW,
  CLEAR_BOTTOM_AFTER_SLOW,
  GO_TOP_FAST,
  BACKOFF_TOP,
  GO_TOP_SLOW,
  CLEAR_TOP_AFTER_SLOW,
  GO_CENTER,
  RESTORE_PROGRESS,
  DONE
};

enum class StartPhase : uint8_t {
  NONE = 0,
  MOVE_TO_EFFECTIVE_BOTTOM
};

// -----------------------------------------------------------------------------
// Data
// -----------------------------------------------------------------------------
struct Config {
  bool     winderCW = true;
  uint32_t rpm = DEFAULT_RPM;
  uint32_t rampRPMperSec = DEFAULT_RAMP;
  uint32_t totalTurns = 10000;
  int32_t  space_um = DEFAULT_SPACE_UM;

  int32_t  offTop_um = 0;
  int32_t  offBottom_um = 0;

  int32_t  softDist_um = DEFAULT_SOFTDIST_UM;
  uint8_t  softPct = DEFAULT_SOFTPCT;

  bool     chaosEnable = DEFAULT_CHAOS_ENABLE;
  uint8_t  chaosPct = DEFAULT_CHAOS_PCT;
  uint16_t chaosPersistTurns = DEFAULT_CHAOS_PERSIST_TURNS;

  uint8_t  tapCount = 0;
  uint32_t tapTargets[3] = {0, 0, 0};

  // Drift compensation: driftCompSteps of traverse-counter correction are spread
  // evenly over every driftCompTurns turns while winding. Positive = the wind was
  // drifting toward TOP (the counter fell behind the carriage). 0 turns = off.
  int32_t  driftCompSteps = 0;
  uint32_t driftCompTurns = 0;
};

struct SetupFlags {
  bool haveDir   = false;
  bool haveRPM   = false;
  bool haveTurns = false;
  bool haveSpace = false;
};

struct Calibration {
  bool hardValid = false;
  bool softValid = false;

  bool softTopSaved = false;
  bool softBottomSaved = false;

  int32_t hardTopSteps = 0;
  int32_t hardBottomSteps = 0;

  int32_t softTopSteps = 0;
  int32_t softBottomSteps = 0;
};

struct RuntimeData {
  int32_t effectiveTopSteps = 0;
  int32_t effectiveBottomSteps = 0;

  TraverseDir traverseDir = TraverseDir::TO_TOP;

  uint32_t turnsDone = 0;
  uint32_t stepInTurn = 0;

  int64_t traverseDemandNumer = 0;
  int32_t travelSinceReverse_um = DEFAULT_SOFTDIST_UM;

  bool stopRequested = false;
  bool pauseRequested = false;
  bool autoStartPending = false;

  uint32_t spinRPM = 0;
  bool spinCW = true;

  bool midActive = false;
  int32_t midInset_um = 0;
  uint32_t midTurnsRemaining = 0;

  bool midArmed = false;
  int32_t midArmedInset_um = 0;
  uint32_t midArmedTurns = 0;

  int8_t activeTapIndex = -1;
  bool tapTriggered[3] = {false, false, false};

  bool autoRecalPending = false;
  uint32_t nextAutoRecalTurn = AUTO_RECAL_INTERVAL_TURNS;

  int32_t chaosSpaceOffset_um = 0;
  uint16_t chaosTurnsRemaining = 0;

  int32_t driftCompAccum = 0;   // fractional driftcomp remainder, in "steps x turns"
};

struct LiveStatus {
  MachineState state = MachineState::IDLE;
  bool permitOK = true;
  bool snagOK = true;
  bool topLimit = false;
  bool bottomLimit = false;
  uint32_t currentRPM = 0;
};

struct RecalSnapshot {
  bool valid = false;

  uint32_t turnsDone = 0;
  uint32_t stepInTurn = 0;
  TraverseDir traverseDir = TraverseDir::TO_TOP;

  bool tapTriggered[3] = {false, false, false};
  int8_t activeTapIndex = -1;
  uint32_t nextAutoRecalTurn = AUTO_RECAL_INTERVAL_TURNS;

  float relativePos = 0.0f;
  int32_t topInsetSteps = 0;
  int32_t bottomInsetSteps = 0;

  // Where the hard switches were found at the PREVIOUS calibration, in the same
  // continuous step frame, so the re-found positions reveal the accumulated drift.
  int32_t oldHardTopSteps = 0;
  int32_t oldHardBottomSteps = 0;
  uint32_t turnMark = 0;
};

struct CalProfile {
  bool valid = false;
  int32_t topInsetSteps = 0;
  int32_t bottomInsetSteps = 0;
  int32_t offTop_um = 0;
  int32_t offBottom_um = 0;
  int32_t softDist_um = DEFAULT_SOFTDIST_UM;
  uint8_t softPct = DEFAULT_SOFTPCT;
  bool chaosEnable = DEFAULT_CHAOS_ENABLE;
  uint8_t chaosPct = DEFAULT_CHAOS_PCT;
};

struct ChaosEntropyInfo {
  uint32_t seedMicros = 0;
  uint32_t seedMillis = 0;
  uint32_t seedAddr = 0;
  uint32_t mixHistory[8] = {0};
  uint32_t finalSeed = 0;
  uint32_t reseedCount = 0;
};

struct TensionData {
  bool enabled = true;
  bool plotEnabled = true;
  bool readySeen = false;

  bool calibrated = false;
  long zero = 0;               // unloaded raw zero, captured by zero command only
  long calRaw = 0;             // raw reading with known reference mass applied
  float refGrams = 0.0f;       // positive magnitude entered by user
  float countsPerGram = 0.0f;  // counts per +1 g wire force; reference mass is -grams

  long raw = 0;
  long force = 0;              // calibrated signed grams-force; wire tension should be positive

  long windowMin = LONG_MAX;   // min calibrated force seen in current 1 s window
  long windowMax = LONG_MIN;   // max calibrated force seen in current 1 s window
  long lastWindowMin = 0;      // value printed/plotted once per second
  long lastWindowMax = 0;
  long lastAvg3s = 0;          // 3-second rolling average of calibrated force

  int64_t windowSum = 0;
  uint32_t windowSamples = 0;

  int64_t avg3Sums[3] = {0, 0, 0};
  uint32_t avg3Samples[3] = {0, 0, 0};
  uint8_t avg3Index = 0;
  uint8_t avg3Filled = 0;

  // Adaptive tension-drop fault monitor.
  // baseline_g is intentionally slow to fall so an abrupt real drop remains detectable.
  bool dropFaultEnabled = true;
  bool dropMonitorArmed = false;
  bool dropCandidateActive = false;
  long dropBaseline_g = 0;
  long dropThreshold_g = 0;
  long dropCandidateForce_g = 0;
  uint32_t dropCandidateStartTurn = 0;
  uint32_t dropConfirmedTurns = 0;

  uint32_t sampleCount = 0;
  uint32_t lastSampleMs = 0;
  uint32_t lastPrintMs = 0;
};

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
Config cfg;
SetupFlags setupFlags;
Calibration cal;
RuntimeData rt;
LiveStatus st;
RecalSnapshot recalSnap;
CalProfile calProfile;
ChaosEntropyInfo chaosEntropy;
TensionData tension;

CalMode calMode = CalMode::MANUAL;
CalFindSubstate calFindSub = CalFindSubstate::GO_BOTTOM_FAST;
StartPhase startPhase = StartPhase::NONE;

bool bottomBackoffArmed = false;
bool topBackoffArmed = false;
int32_t bottomBackoffStart = 0;
int32_t topBackoffStart = 0;
int32_t recalRestoreTargetPos = 0;

MachineState faultReturnState = MachineState::IDLE;
bool faultResumeAvailable = false;

MachineState calReturnState = MachineState::READY;
bool calNeedsRestoreProgress = false;

uint32_t chaosRngState = 0x6D2B79F5u;

// turnsDone at the previous calibration in this run, for RECAL_DRIFT "over_turns".
uint32_t g_prevRecalTurns = 0;

elapsedMicros rampDtUs;
elapsedMillis statusTimer;
elapsedMillis tensionWindowTimer;
String rxLine;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
#if USE_WATCHDOG
void wdtFeed() { wdt.feed(); }
#else
void wdtFeed() {}
#endif

bool permitOK() {
#if HAS_RUN_PERMIT
  return digitalRead(PIN_RUN_PERMIT) == LOW;
#else
  return true;
#endif
}

bool snagOK() {
#if HAS_SNAG_FAULT
  return digitalRead(PIN_SNAG_FAULT) == LOW;
#else
  return true;
#endif
}

struct DebouncedLimitInput {
  uint8_t pin = 255;
  bool activeHigh = true;
  bool stableActive = false;
  bool lastRawActive = false;
  uint32_t lastRawChangeUs = 0;

  void begin(uint8_t p, bool highMeansActive) {
    pin = p;
    activeHigh = highMeansActive;
    bool raw = readRaw();
    stableActive = raw;
    lastRawActive = raw;
    lastRawChangeUs = micros();
  }

  bool readRaw() const {
    bool levelHigh = (digitalRead(pin) == HIGH);
    return activeHigh ? levelHigh : !levelHigh;
  }

  bool readStable() {
    bool raw = readRaw();
    uint32_t now = micros();

    if (raw != lastRawActive) {
      lastRawActive = raw;
      lastRawChangeUs = now;
    }

    if (raw != stableActive && (uint32_t)(now - lastRawChangeUs) >= LIMIT_DEBOUNCE_US) {
      stableActive = raw;
    }

    return stableActive;
  }
};

DebouncedLimitInput topLimitInput;
DebouncedLimitInput bottomLimitInput;

bool topLimitRaw() { return digitalRead(PIN_TOP_LIMIT) == HIGH; }
bool bottomLimitRaw() { return digitalRead(PIN_BOTTOM_LIMIT) == HIGH; }
bool topLimitTriggered() { return topLimitInput.readStable(); }
bool bottomLimitTriggered() { return bottomLimitInput.readStable(); }

const char* stateName(MachineState s) {
  switch (s) {
    case MachineState::IDLE: return "IDLE";
    case MachineState::CAL_FIND_LIMITS: return "CAL_FIND_LIMITS";
    case MachineState::CAL_JOG: return "CAL_JOG";
    case MachineState::READY: return "READY";
    case MachineState::RUNNING_AUTO: return "RUNNING_AUTO";
    case MachineState::RUNNING_SPIN: return "RUNNING_SPIN";
    case MachineState::PAUSED_AUTO: return "PAUSED_AUTO";
    case MachineState::TAP_PAUSE: return "TAP_PAUSE";
    case MachineState::FAULT_FAILSAFE: return "FAULT_FAILSAFE";
    case MachineState::FAULT_SNAG: return "FAULT_SNAG";
    case MachineState::FAULT_TENSION: return "FAULT_TENSION";
    case MachineState::TERMINATED: return "TERMINATED";
    default: return "?";
  }
}

const char* dirName(bool cw) { return cw ? "cw" : "ccw"; }
const char* travDirName(TraverseDir d) { return d == TraverseDir::TO_TOP ? "top" : "bottom"; }

int32_t mmToUm(float mm) { return (int32_t)lroundf(mm * 1000.0f); }
float umToMm(int32_t um) { return ((float)um) / 1000.0f; }
int32_t umToSteps(int32_t um) { return um >= 0 ? um / TRAV_STEP_UM : -((-um) / TRAV_STEP_UM); }
float stepsToMm(int32_t steps) { return ((float)(steps * TRAV_STEP_UM)) / 1000.0f; }

int32_t clampI32(int32_t x, int32_t lo, int32_t hi) {
  if (x < lo) return lo;
  if (x > hi) return hi;
  return x;
}

bool requiredSetupComplete() {
  return setupFlags.haveDir && setupFlags.haveRPM && setupFlags.haveTurns && setupFlags.haveSpace;
}

// One shared definition of "something is in motion / mid-calibration", used to reject
// commands that would be unsafe or blocking while anything is moving.
bool machineMoving() {
  return st.state == MachineState::RUNNING_AUTO ||
         st.state == MachineState::RUNNING_SPIN ||
         st.state == MachineState::CAL_FIND_LIMITS ||
         st.state == MachineState::CAL_JOG;
}

// Active jog direction in CAL_JOG: +1 toward TOP, -1 toward BOTTOM, 0 none.
int8_t jogDir = 0;

void refreshInputs() {
  st.permitOK = permitOK();
  st.snagOK = snagOK();
  st.topLimit = topLimitTriggered();
  st.bottomLimit = bottomLimitTriggered();
}

void clearActiveMid() {
  rt.midActive = false;
  rt.midInset_um = 0;
  rt.midTurnsRemaining = 0;
}

void clearArmedMid() {
  rt.midArmed = false;
  rt.midArmedInset_um = 0;
  rt.midArmedTurns = 0;
}

// -----------------------------------------------------------------------------
// Stepper axis
// -----------------------------------------------------------------------------
struct StepperAxis {
  uint8_t stepPin = 255;
  uint8_t dirPin  = 255;

  bool dirPositive = false;
  bool stepHigh = false;
  bool pulseDirPositive = false;  // DIR level latched at the rising edge of the current pulse
  bool logicalInvert = false;     // DIR_INVERT for this axis; position counts in logical direction

  uint32_t intervalUs = 0;
  uint32_t nextStepUs = 0;
  uint32_t pulseEndUs = 0;
  uint32_t lastDirChangeUs = 0;

  int32_t positionSteps = 0;

  void begin(uint8_t s, uint8_t d, bool initialPositive = false, bool invert = false) {
    stepPin = s;
    dirPin = d;
    logicalInvert = invert;

    pinMode(stepPin, OUTPUT);
    pinMode(dirPin, OUTPUT);

    digitalWrite(stepPin, LOW);
    stepHigh = false;
    positionSteps = 0;

    dirPositive = initialPositive;
    pulseDirPositive = initialPositive;
    digitalWrite(dirPin, dirPositive ? HIGH : LOW);

    uint32_t now = micros();
    nextStepUs = now;
    pulseEndUs = now;
    lastDirChangeUs = now;
  }

  void forceDir(bool positive) {
    dirPositive = positive;
    digitalWrite(dirPin, positive ? HIGH : LOW);
    lastDirChangeUs = micros();
  }

  void setDir(bool positive) {
    if (dirPositive != positive) {
      dirPositive = positive;
      digitalWrite(dirPin, positive ? HIGH : LOW);
      lastDirChangeUs = micros();
    }
  }

  void setRateHz(float hz) {
    if (hz <= 0.0f) {
      intervalUs = 0;
      return;
    }
    float raw = 1000000.0f / hz;
    float minAllowed = (float)(STEP_PULSE_US + DIR_SETUP_US + 2);
    if (raw < minAllowed) raw = minAllowed;

    if (intervalUs == 0) {
      // Waking from stopped. nextStepUs was frozen when the axis stopped; after
      // ~35.8 min the signed compare in update() would see it as "in the future"
      // and the axis would stay silent until micros() wrapped again (~36 min).
      uint32_t now = micros();
      nextStepUs = now;
      pulseEndUs = now;
    }
    intervalUs = (uint32_t)raw;
  }

  // +1 / -1 in LOGICAL direction for the pulse currently (or last) in flight.
  int32_t pulseSign() const { return (pulseDirPositive ^ logicalInvert) ? 1 : -1; }

  void stopImmediate() {
    intervalUs = 0;
    if (stepHigh) {
      // The driver stepped on the rising edge we already emitted; count it
      // instead of discarding it, or position drifts one step per hard stop.
      digitalWrite(stepPin, LOW);
      stepHigh = false;
      positionSteps += pulseSign();
    }
  }

  bool update(uint32_t nowUs) {
    bool stepped = false;

    if (stepHigh && (int32_t)(nowUs - pulseEndUs) >= 0) {
      digitalWrite(stepPin, LOW);
      stepHigh = false;
      positionSteps += pulseSign();
    }

    if (!stepHigh && intervalUs > 0) {
      int32_t due = (int32_t)(nowUs - nextStepUs);
      if (due < -1000000L) {
        // No legitimate step interval is anywhere near 1 s (1 RPM at 400 steps/rev
        // is 150 ms). A "next step" more than 1 s in the future can only be a stale
        // timestamp from before a micros() wrap. Resync instead of going dead.
        nextStepUs = nowUs;
        due = 0;
      }
      if (due >= 0) {
        if ((uint32_t)(nowUs - lastDirChangeUs) >= DIR_SETUP_US) {
          pulseDirPositive = dirPositive;  // latch: the driver samples DIR at this edge
          digitalWrite(stepPin, HIGH);
          stepHigh = true;
          pulseEndUs = nowUs + STEP_PULSE_US;
          nextStepUs = nowUs + intervalUs;
          stepped = true;
        }
      }
    }

    return stepped;
  }
};

StepperAxis winder;
StepperAxis traverse;

float currentWinderRPM = 0.0f;
float commandedWinderRPM = 0.0f;

// -----------------------------------------------------------------------------
// Parsing helpers
// -----------------------------------------------------------------------------
void trimLine(String& s) { s.trim(); }

String nextToken(const String& s, int& start) {
  while (start < (int)s.length() && s[start] == ' ') start++;
  if (start >= (int)s.length()) return "";
  int end = start;
  while (end < (int)s.length() && s[end] != ' ') end++;
  String out = s.substring(start, end);
  start = end;
  while (start < (int)s.length() && s[start] == ' ') start++;
  return out;
}

bool parseUInt(const String& s, uint32_t& out) {
  if (s.length() == 0) return false;
  char* endp = nullptr;
  unsigned long v = strtoul(s.c_str(), &endp, 10);
  if (*endp != '\0') return false;
  out = (uint32_t)v;
  return true;
}

bool parseInt32Val(const String& s, int32_t& out) {
  if (s.length() == 0) return false;
  char* endp = nullptr;
  long v = strtol(s.c_str(), &endp, 10);
  if (*endp != '\0') return false;
  out = (int32_t)v;
  return true;
}

bool parseFloatVal(const String& s, float& out) {
  if (s.length() == 0) return false;
  char* endp = nullptr;
  out = strtof(s.c_str(), &endp);
  if (*endp != '\0') return false;
  return true;
}

// -----------------------------------------------------------------------------
// Forward declarations
// -----------------------------------------------------------------------------
void updateEffectiveBounds();
void hardStopNow();
void enterState(MachineState s);
void handleWinderStepEvents();
void printStatus();
void normalizeTapConfig(bool autoFillCount = true);
void processCommand(String line);
void startCalibration(CalMode mode);
void updateTension();
bool tensionReadAverage(long& avg, uint16_t samples = TENSION_CAL_SAMPLES, uint32_t timeoutMs = 3000UL);
void tensionZero(uint16_t samples = TENSION_CAL_SAMPLES);
void tensionTareKnownMass(float grams, uint16_t samples = TENSION_CAL_SAMPLES);
long tensionForceFromRaw(long raw);
void printTensionStatus();
void prepareTensionForRunStart();
void resetTensionDropFaultMonitor();
void updateTensionDropFaultMonitorOnTurn();
void enterTensionDropFault();
void printRecalDrift();

// -----------------------------------------------------------------------------
// Command port
// -----------------------------------------------------------------------------
void serviceUsbCommandPort(Stream& port, String& buf) {
  while (port.available() > 0) {
    char c = (char)port.read();
    if (c == '\r') continue;
    if (c == '\n') {
      processCommand(buf);
      buf = "";
    } else {
      buf += c;
      if (buf.length() > 500) buf = "";
    }
  }
}

// -----------------------------------------------------------------------------
// Chaos RNG / entropy
// -----------------------------------------------------------------------------
uint32_t hwEntropy32() {
  uint32_t hi = (uint32_t)Entropy.random(0x10000L);
  uint32_t lo = (uint32_t)Entropy.random(0x10000L);
  return (hi << 16) | lo;
}

uint32_t nextChaos32() {
  if (chaosRngState == 0) chaosRngState = hwEntropy32() ^ 0xA341316Cu;
  uint32_t x = chaosRngState;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  chaosRngState = x;
  return x;
}

void seedChaosRng() {
  chaosEntropy.seedMicros = micros();
  chaosEntropy.seedMillis = millis();
  chaosEntropy.seedAddr = (uint32_t)(uintptr_t)&chaosRngState;

  uint32_t seed = hwEntropy32();

  for (int i = 0; i < 8; ++i) {
    uint32_t mix = hwEntropy32();
    seed ^= mix;
    seed ^= (seed << 13);
    seed ^= (seed >> 17);
    seed ^= (seed << 5);
    chaosEntropy.mixHistory[i] = mix;
  }

  if (seed == 0) seed = 0x9E3779B9u;
  chaosEntropy.finalSeed = seed;
  chaosEntropy.reseedCount++;
  chaosRngState = seed;
}

void printEntropyInfo() {
  Serial.println();
  Serial.println("=== CHAOS ENTROPY ===");
  Serial.print("SEED_MICROS: "); Serial.println(chaosEntropy.seedMicros);
  Serial.print("SEED_MILLIS: "); Serial.println(chaosEntropy.seedMillis);
  Serial.print("SEED_ADDR: "); Serial.println(chaosEntropy.seedAddr);
  for (int i = 0; i < 8; ++i) {
    Serial.print("MIX["); Serial.print(i); Serial.print("]: ");
    Serial.println(chaosEntropy.mixHistory[i]);
  }
  Serial.print("FINAL_SEED: "); Serial.println(chaosEntropy.finalSeed);
  Serial.print("RESEED_COUNT: "); Serial.println(chaosEntropy.reseedCount);
  Serial.print("RNG_STATE_NOW: "); Serial.println(chaosRngState);
  Serial.println();
}

int32_t getChaosMaxOffsetUm() {
  if (!cfg.chaosEnable || cfg.chaosPct == 0) return 0;
  int32_t maxUm = (int32_t)lroundf(((float)cfg.space_um * (float)cfg.chaosPct) / 100.0f);
  if (maxUm < 1) maxUm = 1;
  return maxUm;
}

void chooseNewChaosSpacingOffset() {
  if (!cfg.chaosEnable || cfg.chaosPct == 0) {
    rt.chaosSpaceOffset_um = 0;
    rt.chaosTurnsRemaining = 0;
    return;
  }

  int32_t maxUm = getChaosMaxOffsetUm();
  uint32_t r = nextChaos32();
  int32_t span = (maxUm * 2) + 1;
  int32_t offset = ((int32_t)(r % (uint32_t)span)) - maxUm;

  rt.chaosSpaceOffset_um = offset;
  rt.chaosTurnsRemaining = cfg.chaosPersistTurns;
}

int32_t getEffectiveSpaceUm() {
  int32_t effective = cfg.space_um + rt.chaosSpaceOffset_um;
  if (effective < 0) effective = 0;
  return effective;
}

// -----------------------------------------------------------------------------
// HX711 tension reader
// -----------------------------------------------------------------------------
void hx711Begin() {
  pinMode(PIN_HX711_DT, INPUT_PULLUP);
  pinMode(PIN_HX711_SCK, OUTPUT);
  digitalWrite(PIN_HX711_SCK, LOW);  // HIGH > ~60 us powers down HX711, so keep idle LOW
}

bool hx711Ready() {
  return digitalRead(PIN_HX711_DT) == LOW;
}

bool hx711ReadRaw(long& out) {
  if (!hx711Ready()) return false;

  uint32_t value = 0;

  // Keep this short so it does not disturb winder step timing.
  // 500 ns pulse spacing is comfortably above the HX711's 200 ns minimum and
  // roughly halves the blocking time of the old 1 us version (~26 us total).
  for (uint8_t i = 0; i < 24; ++i) {
    digitalWriteFast(PIN_HX711_SCK, HIGH);
    delayNanoseconds(HX711_PULSE_NS);
    value = (value << 1) | (digitalReadFast(PIN_HX711_DT) ? 1UL : 0UL);
    digitalWriteFast(PIN_HX711_SCK, LOW);
    delayNanoseconds(HX711_PULSE_NS);
  }

  // 25th pulse selects channel A, gain 128 for the next conversion.
  digitalWriteFast(PIN_HX711_SCK, HIGH);
  delayNanoseconds(HX711_PULSE_NS);
  digitalWriteFast(PIN_HX711_SCK, LOW);
  delayNanoseconds(HX711_PULSE_NS);

  // Sign extend 24-bit two's-complement reading to 32-bit signed long.
  if (value & 0x800000UL) value |= 0xFF000000UL;
  out = (long)((int32_t)value);
  return true;
}

void tensionResetWindow() {
  tension.windowMin = LONG_MAX;
  tension.windowMax = LONG_MIN;
  tension.windowSum = 0;
  tension.windowSamples = 0;
  tension.avg3Sums[0] = tension.avg3Sums[1] = tension.avg3Sums[2] = 0;
  tension.avg3Samples[0] = tension.avg3Samples[1] = tension.avg3Samples[2] = 0;
  tension.avg3Index = 0;
  tension.avg3Filled = 0;
  tension.lastWindowMin = 0;
  tension.lastWindowMax = 0;
  tension.lastAvg3s = 0;
  tensionWindowTimer = 0;
}

long tensionForceFromRaw(long raw) {
  if (tension.calibrated && fabsf(tension.countsPerGram) > 0.000001f) {
    return (long)lroundf(((float)(raw - tension.zero)) / tension.countsPerGram);
  }

  // Before tare <grams>, report signed raw-count delta instead of pretending it is calibrated.
  return raw - tension.zero;
}

bool tensionReadAverage(long& avg, uint16_t samples, uint32_t timeoutMs) {
  if (samples == 0) samples = 1;

  int64_t sum = 0;
  uint16_t got = 0;
  uint32_t startMs = millis();

  while (got < samples && (millis() - startMs) < timeoutMs) {
    long v = 0;
    if (hx711ReadRaw(v)) {
      sum += v;
      got++;
    }
    wdtFeed();
    delay(10);
  }

  if (got == 0) return false;

  avg = (long)(sum / (int64_t)got);
  tension.raw = avg;
  tension.readySeen = true;
  return true;
}

void tensionZero(uint16_t samples) {
  long avg = 0;
  if (!tensionReadAverage(avg, samples, 3000UL)) {
    Serial.println("ERR tension zero failed: HX711 not ready");
    return;
  }

  tension.zero = avg;
  tension.raw = avg;
  tension.force = 0;

  // A new unloaded zero invalidates the previous reference-mass slope.
  tension.calibrated = false;
  tension.calRaw = 0;
  tension.refGrams = 0.0f;
  tension.countsPerGram = 0.0f;

  tensionResetWindow();
  resetTensionDropFaultMonitor();
  Serial.print("OK tension zero raw=");
  Serial.print(tension.zero);
  Serial.print(" samples=");
  Serial.println(samples);
}

void tensionTareKnownMass(float grams, uint16_t samples) {
  if (grams <= 0.0f) {
    Serial.println("ERR use tare <grams> with a positive known mass value");
    return;
  }

  long avg = 0;
  if (!tensionReadAverage(avg, samples, 3000UL)) {
    Serial.println("ERR tension tare failed: HX711 not ready");
    return;
  }

  long deltaCounts = avg - tension.zero;
  if (deltaCounts == 0) {
    Serial.println("ERR tension tare failed: reference mass produced no raw-count change");
    return;
  }

  tension.calRaw = avg;
  tension.refGrams = grams;

  // The known mass pulls opposite the wire force, so its physical reference is -grams.
  // This makes later wire-applied force read positive when it pulls the opposite way.
  tension.countsPerGram = ((float)deltaCounts) / (-grams);
  tension.calibrated = true;
  tension.force = tensionForceFromRaw(avg);

  tensionResetWindow();
  resetTensionDropFaultMonitor();
  Serial.print("OK tension calibrated reference_g=-");
  Serial.print(grams, 3);
  Serial.print(" zero_raw=");
  Serial.print(tension.zero);
  Serial.print(" ref_raw=");
  Serial.print(tension.calRaw);
  Serial.print(" counts_per_g=");
  Serial.print(tension.countsPerGram, 6);
  Serial.print(" ref_force_g=");
  Serial.print(tension.force);
  Serial.print(" samples=");
  Serial.println(samples);
}

void updateTension() {
  if (!tension.enabled) return;

  long raw = 0;
  if (hx711ReadRaw(raw)) {
    tension.raw = raw;
    tension.force = tensionForceFromRaw(raw);
    tension.readySeen = true;
    tension.sampleCount++;
    tension.lastSampleMs = millis();

    if (tension.force < tension.windowMin) tension.windowMin = tension.force;
    if (tension.force > tension.windowMax) tension.windowMax = tension.force;
    tension.windowSum += tension.force;
    tension.windowSamples++;
  }

  if (tensionWindowTimer >= TENSION_WINDOW_MS) {
    tensionWindowTimer = 0;

    tension.lastWindowMin = (tension.windowSamples > 0 && tension.windowMin != LONG_MAX) ? tension.windowMin : 0;
    tension.lastWindowMax = (tension.windowSamples > 0 && tension.windowMax != LONG_MIN) ? tension.windowMax : 0;

    tension.avg3Sums[tension.avg3Index] = tension.windowSum;
    tension.avg3Samples[tension.avg3Index] = tension.windowSamples;
    tension.avg3Index = (uint8_t)((tension.avg3Index + 1) % 3);
    if (tension.avg3Filled < 3) tension.avg3Filled++;

    int64_t sum3 = 0;
    uint32_t samples3 = 0;
    for (uint8_t i = 0; i < tension.avg3Filled; ++i) {
      sum3 += tension.avg3Sums[i];
      samples3 += tension.avg3Samples[i];
    }
    tension.lastAvg3s = samples3 > 0 ? (long)(sum3 / (int64_t)samples3) : 0;

    tension.windowMin = LONG_MAX;
    tension.windowMax = LONG_MIN;
    tension.windowSum = 0;
    tension.windowSamples = 0;
    tension.lastPrintMs = millis();

    // Arduino Serial Plotter friendly output.
    // After zero + tare <grams>, values are calibrated grams-force.
    // Before calibration, values are signed raw-count delta from zero.
    if (tension.plotEnabled) {
      if (tension.calibrated) Serial.print("Min1s_g:");
      else Serial.print("Min1s_raw:");
      Serial.print(tension.lastWindowMin);
      if (tension.calibrated) Serial.print(" Max1s_g:");
      else Serial.print(" Max1s_raw:");
      Serial.print(tension.lastWindowMax);
      if (tension.calibrated) Serial.print(" Avg3s_g:");
      else Serial.print(" Avg3s_raw:");
      Serial.println(tension.lastAvg3s);
    }
  }
}

void printTensionStatus() {
  Serial.println();
  Serial.println("=== TENSION STATUS ===");
  Serial.print("TENSION_ENABLED: "); Serial.println(tension.enabled ? 1 : 0);
  Serial.print("TENSION_PLOT: "); Serial.println(tension.plotEnabled ? 1 : 0);
  Serial.print("HX711_READY_NOW: "); Serial.println(hx711Ready() ? 1 : 0);
  Serial.print("HX711_READY_SEEN: "); Serial.println(tension.readySeen ? 1 : 0);
  Serial.print("RAW: "); Serial.println(tension.raw);
  Serial.print("ZERO_RAW: "); Serial.println(tension.zero);
  Serial.print("CALIBRATED: "); Serial.println(tension.calibrated ? 1 : 0);
  Serial.print("REFERENCE_GRAMS: "); Serial.println(tension.refGrams, 3);
  Serial.print("REFERENCE_RAW: "); Serial.println(tension.calRaw);
  Serial.print("COUNTS_PER_GRAM: "); Serial.println(tension.countsPerGram, 6);
  Serial.print(tension.calibrated ? "FORCE_G: " : "FORCE_RAW_DELTA: "); Serial.println(tension.force);
  Serial.print(tension.calibrated ? "MIN_1S_G: " : "MIN_1S_RAW: "); Serial.println(tension.lastWindowMin);
  Serial.print(tension.calibrated ? "MAX_1S_G: " : "MAX_1S_RAW: "); Serial.println(tension.lastWindowMax);
  Serial.print(tension.calibrated ? "AVG_3S_G: " : "AVG_3S_RAW: "); Serial.println(tension.lastAvg3s);
  Serial.print("DROP_FAULT_ENABLED: "); Serial.println(tension.dropFaultEnabled ? 1 : 0);
  Serial.print("DROP_MONITOR_ARMED: "); Serial.println(tension.dropMonitorArmed ? 1 : 0);
  Serial.print("DROP_BASELINE_G: "); Serial.println(tension.dropBaseline_g);
  Serial.print("DROP_THRESHOLD_G: "); Serial.println(tension.dropThreshold_g);
  Serial.print("DROP_CANDIDATE_ACTIVE: "); Serial.println(tension.dropCandidateActive ? 1 : 0);
  Serial.print("DROP_CANDIDATE_START_TURN: "); Serial.println(tension.dropCandidateStartTurn);
  Serial.print("DROP_CONFIRMED_TURNS: "); Serial.println(tension.dropConfirmedTurns);
  Serial.print("DROP_CONFIRM_TURNS_REQUIRED: "); Serial.println(TENSION_DROP_CONFIRM_TURNS);
  Serial.print("DROP_KEEP_PCT: "); Serial.println(TENSION_DROP_KEEP_PCT);
  Serial.print("DROP_BASELINE_MIN_G: "); Serial.println(TENSION_DROP_BASELINE_MIN_G);
  Serial.print("SAMPLE_COUNT: "); Serial.println(tension.sampleCount);
  Serial.print("MS_SINCE_SAMPLE: "); Serial.println(millis() - tension.lastSampleMs);
  Serial.println();
}

void resetTensionDropFaultMonitor() {
  tension.dropMonitorArmed = false;
  tension.dropCandidateActive = false;
  tension.dropBaseline_g = 0;
  tension.dropThreshold_g = 0;
  tension.dropCandidateForce_g = 0;
  tension.dropCandidateStartTurn = 0;
  tension.dropConfirmedTurns = 0;
}

void updateTensionDropFaultMonitorOnTurn() {
  if (!tension.dropFaultEnabled) return;
  if (!tension.enabled || !tension.calibrated) return;
  if (st.state != MachineState::RUNNING_AUTO) return;
  if (!tension.readySeen) return;
  if ((millis() - tension.lastSampleMs) > TENSION_STALE_MS) return;
  if (rt.turnsDone < TENSION_DROP_IGNORE_START_TURNS) return;

  long f = tension.force;
  if (f < 0) f = 0;

  // Establish and maintain an adaptive normal-running baseline.
  // It rises quickly with higher real tension, but falls slowly only when not in a drop candidate.
  if (!tension.dropMonitorArmed) {
    if (f >= TENSION_DROP_BASELINE_MIN_G) {
      tension.dropBaseline_g = f;
      tension.dropMonitorArmed = true;
    }
    return;
  }

  if (f > tension.dropBaseline_g) {
    // Slew-limited rise: a single high sample can no longer yank the baseline
    // (and with it the fault threshold) up to a transient spike.
    long rise = (f - tension.dropBaseline_g) / 8;
    if (rise < 1) rise = 1;
    tension.dropBaseline_g += rise;
  } else if (f < tension.dropBaseline_g && !tension.dropCandidateActive) {
    // Guaranteed-progress decay: the old (f - baseline)/16 truncated to zero for
    // any deficit under 16 g, so the baseline could never fall and slow
    // legitimate tension drift would eventually trip a false fault.
    long fall = (tension.dropBaseline_g - f) / 16;
    if (fall < 1) fall = 1;
    tension.dropBaseline_g -= fall;
  }

  if (tension.dropBaseline_g < TENSION_DROP_BASELINE_MIN_G) {
    resetTensionDropFaultMonitor();
    return;
  }

  long threshold = (tension.dropBaseline_g * (long)TENSION_DROP_KEEP_PCT) / 100L;
  if (threshold < 1) threshold = 1;
  tension.dropThreshold_g = threshold;

  bool lowEnough = (f <= threshold);

  if (lowEnough) {
    if (!tension.dropCandidateActive) {
      tension.dropCandidateActive = true;
      tension.dropCandidateStartTurn = rt.turnsDone;
      tension.dropCandidateForce_g = f;
      tension.dropConfirmedTurns = 0;
    } else {
      if (f < tension.dropCandidateForce_g) tension.dropCandidateForce_g = f;
      tension.dropConfirmedTurns = rt.turnsDone - tension.dropCandidateStartTurn;
    }

    if (tension.dropConfirmedTurns >= TENSION_DROP_CONFIRM_TURNS) {
      enterTensionDropFault();
    }
  } else {
    tension.dropCandidateActive = false;
    tension.dropCandidateStartTurn = 0;
    tension.dropCandidateForce_g = 0;
    tension.dropConfirmedTurns = 0;
  }
}

// -----------------------------------------------------------------------------
// Calibration profile
// -----------------------------------------------------------------------------
void applyStandardDefaults() {
  cfg.rpm = 500;
  cfg.rampRPMperSec = DEFAULT_RAMP;
  cfg.space_um = DEFAULT_SPACE_UM;        // 1.000 mm
  cfg.softDist_um = DEFAULT_SOFTDIST_UM;  // 0.500 mm
  cfg.softPct = DEFAULT_SOFTPCT;

  cfg.chaosEnable = DEFAULT_CHAOS_ENABLE;
  cfg.chaosPct = DEFAULT_CHAOS_PCT;
  cfg.chaosPersistTurns = DEFAULT_CHAOS_PERSIST_TURNS;

  cfg.offTop_um = DEFAULT_CAL_OFFTOP_UM;
  cfg.offBottom_um = DEFAULT_CAL_OFFBOTTOM_UM;

  setupFlags.haveRPM = true;
  setupFlags.haveSpace = true;
}

void applyStandardDefaultProfile() {
  calProfile.valid = true;
  calProfile.topInsetSteps = DEFAULT_PROFILE_TOP_INSET_STEPS;
  calProfile.bottomInsetSteps = DEFAULT_PROFILE_BOTTOM_INSET_STEPS;
  calProfile.offTop_um = DEFAULT_CAL_OFFTOP_UM;
  calProfile.offBottom_um = DEFAULT_CAL_OFFBOTTOM_UM;
  calProfile.softDist_um = DEFAULT_PROFILE_SOFTDIST_UM;
  calProfile.softPct = DEFAULT_PROFILE_SOFTPCT;
  calProfile.chaosEnable = DEFAULT_PROFILE_CHAOS_ENABLE;
  calProfile.chaosPct = DEFAULT_PROFILE_CHAOS_PCT;
}

void printDefcalProfileString() {
  // Built from the live constants so the banner can never disagree with what is applied.
  Serial.print("CALPROFILE_V2,");
  Serial.print(DEFAULT_PROFILE_TOP_INSET_STEPS); Serial.print(",");
  Serial.print(DEFAULT_PROFILE_BOTTOM_INSET_STEPS); Serial.print(",");
  Serial.print(DEFAULT_CAL_OFFTOP_UM); Serial.print(",");
  Serial.print(DEFAULT_CAL_OFFBOTTOM_UM); Serial.print(",");
  Serial.print(DEFAULT_PROFILE_SOFTDIST_UM); Serial.print(",");
  Serial.print(DEFAULT_PROFILE_SOFTPCT); Serial.print(",");
  Serial.print(DEFAULT_PROFILE_CHAOS_ENABLE ? 1 : 0); Serial.print(",");
  Serial.print(DEFAULT_PROFILE_CHAOS_PCT); Serial.print(",");
  Serial.print(DEFAULT_CHAOS_PERSIST_TURNS); Serial.print(",");
  Serial.println(DEFAULT_RAMP);
}

bool startDefaultDefcal(bool verbose = true) {
  if (machineMoving()) {
    if (verbose) Serial.println("ERR cannot start defcal while machine is moving/calibrating");
    return false;
  }

  applyStandardDefaults();
  applyStandardDefaultProfile();

  // Allow standalone defcal to run even before dir/turns are set.
  setupFlags.haveDir = true;
  setupFlags.haveTurns = true;

  startCalibration(CalMode::PROFILE_TEMPLATE);
  if (verbose) {
    Serial.print("OK defcal started: finding hard limits, then applying ");
    printDefcalProfileString();
  }
  return true;
}

void normalizeTapConfig(bool autoFillCount) {
  // Remove impossible taps and keep them ascending so taps entered in any
  // order behave the same.
  for (uint8_t i = 0; i < 3; ++i) {
    if (cfg.tapTargets[i] == 0) continue;
    if (cfg.tapTargets[i] >= cfg.totalTurns) {
      cfg.tapTargets[i] = 0;
    }
  }

  // Simple ascending sort of non-zero taps.
  for (uint8_t i = 0; i < 3; ++i) {
    for (uint8_t j = i + 1; j < 3; ++j) {
      uint32_t a = cfg.tapTargets[i];
      uint32_t b = cfg.tapTargets[j];

      if (a == 0 && b != 0) {
        cfg.tapTargets[i] = b;
        cfg.tapTargets[j] = 0;
      } else if (a != 0 && b != 0 && b < a) {
        cfg.tapTargets[i] = b;
        cfg.tapTargets[j] = a;
      }
    }
  }

  if (autoFillCount) {
    uint8_t count = 0;
    while (count < 3 && cfg.tapTargets[count] != 0) count++;
    cfg.tapCount = count;
  } else {
    if (cfg.tapCount > 3) cfg.tapCount = 3;
    for (uint8_t i = cfg.tapCount; i < 3; ++i) cfg.tapTargets[i] = 0;
  }
}

void loadDefaultProfileIntoMemory() {
  applyStandardDefaultProfile();
}

bool applyStoredCalProfileToCurrentHardBounds() {
  if (!calProfile.valid) {
    Serial.println("ERR no valid cal profile loaded");
    return false;
  }
  if (!cal.hardValid) {
    Serial.println("ERR hard bounds not valid");
    return false;
  }

  cal.softTopSteps = cal.hardTopSteps - calProfile.topInsetSteps;
  cal.softBottomSteps = cal.hardBottomSteps + calProfile.bottomInsetSteps;

  if (cal.softTopSteps <= cal.softBottomSteps) {
    Serial.println("ERR default cal profile invalid for current hard bounds");
    return false;
  }

  cal.softValid = true;
  cal.softTopSaved = true;
  cal.softBottomSaved = true;

  cfg.offTop_um = calProfile.offTop_um;
  cfg.offBottom_um = calProfile.offBottom_um;
  cfg.softDist_um = calProfile.softDist_um;
  cfg.softPct = calProfile.softPct;
  cfg.chaosEnable = calProfile.chaosEnable;
  cfg.chaosPct = calProfile.chaosPct;
  cfg.chaosPersistTurns = DEFAULT_CHAOS_PERSIST_TURNS;
  cfg.rampRPMperSec = DEFAULT_RAMP;

  updateEffectiveBounds();

  Serial.println("OK defcal profile applied to newly found hard limits");
  Serial.print("HARD_TOP="); Serial.println(cal.hardTopSteps);
  Serial.print("HARD_BOTTOM="); Serial.println(cal.hardBottomSteps);
  Serial.print("SOFT_TOP="); Serial.println(cal.softTopSteps);
  Serial.print("SOFT_BOTTOM="); Serial.println(cal.softBottomSteps);
  Serial.print("ACTIVE_TOP="); Serial.println(rt.effectiveTopSteps);
  Serial.print("ACTIVE_BOTTOM="); Serial.println(rt.effectiveBottomSteps);
  return true;
}

void updateStoredCalProfileFromCurrentCalibration() {
  // Disabled: do not store/reuse calibration profile values after hardware moved.
}

void exportCalProfile() {
  // Older workflow used exportcal after saving bounds.
  // Now it prints the rebuild/status block instead of failing.
  printStatus();
}

bool importCalProfileStringOnly(const String& payload) {
  (void)payload;
  return false;
}

void clearCalProfile() {
  calProfile = CalProfile();
  Serial.println("OK cal profile cleared");
}

// -----------------------------------------------------------------------------
// Motion helpers
// -----------------------------------------------------------------------------
void hardStopNow() {
  winder.stopImmediate();
  traverse.stopImmediate();
  commandedWinderRPM = 0.0f;
  currentWinderRPM = 0.0f;
  st.currentRPM = 0;
}

void enterState(MachineState s) {
  st.state = s;
  if (s == MachineState::READY ||
      s == MachineState::PAUSED_AUTO ||
      s == MachineState::TAP_PAUSE ||
      s == MachineState::FAULT_FAILSAFE ||
      s == MachineState::FAULT_SNAG ||
      s == MachineState::FAULT_TENSION ||
      s == MachineState::IDLE ||
      s == MachineState::TERMINATED) {
    traverse.setRateHz(0.0f);
  }
}

void applyWinderRPM(float rpm) {
  if (rpm <= 0.0f) {
    winder.setRateHz(0.0f);
    currentWinderRPM = 0.0f;
    st.currentRPM = 0;
    return;
  }

  float stepsPerMinute = rpm * (float)WINDER_STEPS_PER_REV;
  float hz = stepsPerMinute / 60.0f;
  winder.setRateHz(hz);
  currentWinderRPM = rpm;
  st.currentRPM = (uint32_t)(rpm + 0.5f);
}

void updateWinderRamp() {
  float dt = rampDtUs / 1000000.0f;
  rampDtUs = 0;
  float delta = (float)cfg.rampRPMperSec * dt;

  if (currentWinderRPM < commandedWinderRPM) {
    currentWinderRPM += delta;
    if (currentWinderRPM > commandedWinderRPM) currentWinderRPM = commandedWinderRPM;
  } else if (currentWinderRPM > commandedWinderRPM) {
    currentWinderRPM -= delta;
    if (currentWinderRPM < commandedWinderRPM) currentWinderRPM = commandedWinderRPM;
  }

  if (commandedWinderRPM <= 0.0f && currentWinderRPM < STOP_RPM_EPS) {
    currentWinderRPM = 0.0f;
  }

  applyWinderRPM(currentWinderRPM);
}

void beginWinderWithVisibleFloor(uint32_t targetRPM) {
  uint32_t initialRPM = targetRPM > START_FLOOR_RPM ? START_FLOOR_RPM : targetRPM;
  commandedWinderRPM = (float)targetRPM;
  currentWinderRPM = (float)initialRPM;
  applyWinderRPM(currentWinderRPM);
  rampDtUs = 0;
}

void setTraverseRPM(uint32_t rpm, TraverseDir dir) {
  bool logicalTop = (dir == TraverseDir::TO_TOP);
  traverse.setDir(logicalTop ^ TRAVERSE_DIR_INVERT);
  float hz = ((float)rpm * (float)TRAV_STEPS_PER_REV) / 60.0f;
  traverse.setRateHz(hz);
}

void stopTraverse() {
  traverse.setRateHz(0.0f);
}

void updateEffectiveBounds() {
  if (!cal.softValid) return;

  int32_t offTopSteps = umToSteps(cfg.offTop_um);
  int32_t offBottomSteps = umToSteps(cfg.offBottom_um);

  rt.effectiveTopSteps = cal.softTopSteps - offTopSteps;
  rt.effectiveBottomSteps = cal.softBottomSteps + offBottomSteps;

  if (cal.hardValid) {
    rt.effectiveTopSteps =
        clampI32(rt.effectiveTopSteps, cal.hardBottomSteps + 1, cal.hardTopSteps);
    rt.effectiveBottomSteps =
        clampI32(rt.effectiveBottomSteps, cal.hardBottomSteps, cal.hardTopSteps - 1);
  }

  if (rt.effectiveTopSteps <= rt.effectiveBottomSteps) {
    rt.effectiveTopSteps = rt.effectiveBottomSteps + 1;
  }
}

void getActiveBounds(int32_t& activeTopSteps, int32_t& activeBottomSteps) {
  activeTopSteps = rt.effectiveTopSteps;
  activeBottomSteps = rt.effectiveBottomSteps;

  if (!rt.midActive || rt.midTurnsRemaining == 0) return;

  int32_t insetSteps = umToSteps(rt.midInset_um);
  activeTopSteps -= insetSteps;
  activeBottomSteps += insetSteps;

  if (activeTopSteps <= activeBottomSteps) {
    activeTopSteps = activeBottomSteps + 1;
  }
}

bool atEffectiveTop() {
  int32_t t, b;
  getActiveBounds(t, b);
  return traverse.positionSteps >= t;
}

bool atEffectiveBottom() {
  int32_t t, b;
  getActiveBounds(t, b);
  return traverse.positionSteps <= b;
}

float getTraverseScale() {
  if (cfg.softDist_um <= 0 || cfg.softPct >= 100 || !cal.softValid) return 1.0f;

  float minScale = ((float)cfg.softPct) / 100.0f;
  int32_t pos = traverse.positionSteps;

  int32_t activeTop, activeBottom;
  getActiveBounds(activeTop, activeBottom);

  int32_t topDist_um = (activeTop - pos) * TRAV_STEP_UM;
  int32_t bottomDist_um = (pos - activeBottom) * TRAV_STEP_UM;
  int32_t approachDist_um = (rt.traverseDir == TraverseDir::TO_TOP) ? topDist_um : bottomDist_um;

  float approachScale;
  if (approachDist_um <= 0) approachScale = minScale;
  else if (approachDist_um >= cfg.softDist_um) approachScale = 1.0f;
  else {
    float t = ((float)approachDist_um) / ((float)cfg.softDist_um);
    approachScale = minScale + (1.0f - minScale) * t;
  }

  float departScale;
  if (rt.travelSinceReverse_um <= 0) departScale = minScale;
  else if (rt.travelSinceReverse_um >= cfg.softDist_um) departScale = 1.0f;
  else {
    float t = ((float)rt.travelSinceReverse_um) / ((float)cfg.softDist_um);
    departScale = minScale + (1.0f - minScale) * t;
  }

  return min(approachScale, departScale);
}

void recordReversal() {
  rt.travelSinceReverse_um = 0;
}

void emitTraverseSingleStep(TraverseDir dir) {
  if (dir == TraverseDir::TO_TOP) {
    if (topLimitTriggered()) return;
    if (cal.softValid && atEffectiveTop()) return;
  } else {
    if (bottomLimitTriggered()) return;
    if (cal.softValid && atEffectiveBottom()) return;
  }

  // Only a real reversal gets the long DIR settle. Same-direction steps keep the
  // 5 us setup so the winder step path is not slowed.
  bool wantPositive = ((dir == TraverseDir::TO_TOP) ? true : false) ^ TRAVERSE_DIR_INVERT;
  bool reversing = (traverse.dirPositive != wantPositive);
  traverse.setDir(wantPositive);
  delayMicroseconds(reversing ? TRAV_REVERSAL_DIR_SETTLE_US : DIR_SETUP_US);

  digitalWrite(PIN_TRAV_STEP, HIGH);
  delayMicroseconds(STEP_PULSE_US);
  digitalWrite(PIN_TRAV_STEP, LOW);

  traverse.positionSteps += (dir == TraverseDir::TO_TOP) ? 1 : -1;

  if (rt.travelSinceReverse_um < cfg.softDist_um) {
    rt.travelSinceReverse_um += TRAV_STEP_UM;
    if (rt.travelSinceReverse_um > cfg.softDist_um) rt.travelSinceReverse_um = cfg.softDist_um;
  }
}

void nudgeTraverseMm(float mm, TraverseDir dir) {
  int32_t steps = umToSteps(mmToUm(mm));
  if (steps < 0) steps = -steps;
  if (steps == 0) return;

  if (dir == TraverseDir::TO_TOP) traverse.setDir(true ^ TRAVERSE_DIR_INVERT);
  else traverse.setDir(false ^ TRAVERSE_DIR_INVERT);

  delayMicroseconds(NUDGE_DIR_SETTLE_US);

  for (int32_t i = 0; i < steps; ++i) {
    // A nudge blocks for seconds; abort immediately if a safety input trips.
    if (!permitOK() || !snagOK()) break;
    wdtFeed();

    if (dir == TraverseDir::TO_TOP) {
      if (topLimitTriggered()) break;
    } else {
      if (bottomLimitTriggered()) break;
    }

    if (cal.softValid) {
      if (dir == TraverseDir::TO_TOP && atEffectiveTop()) break;
      if (dir == TraverseDir::TO_BOTTOM && atEffectiveBottom()) break;
    }

    digitalWrite(PIN_TRAV_STEP, HIGH);
    delayMicroseconds(STEP_PULSE_US);
    digitalWrite(PIN_TRAV_STEP, LOW);

    if (dir == TraverseDir::TO_TOP) traverse.positionSteps += 1;
    else traverse.positionSteps -= 1;

    if (rt.travelSinceReverse_um < cfg.softDist_um) {
      rt.travelSinceReverse_um += TRAV_STEP_UM;
      if (rt.travelSinceReverse_um > cfg.softDist_um) rt.travelSinceReverse_um = cfg.softDist_um;
    }

    delayMicroseconds(NUDGE_STEP_DELAY_US);
  }
}

void resetAutoProgress() {
  rt.turnsDone = 0;
  rt.stepInTurn = 0;
  rt.traverseDemandNumer = 0;
  rt.stopRequested = false;
  rt.pauseRequested = false;
  rt.autoStartPending = false;
  rt.traverseDir = TraverseDir::TO_TOP;
  rt.travelSinceReverse_um = 0;
  rt.activeTapIndex = -1;
  rt.tapTriggered[0] = rt.tapTriggered[1] = rt.tapTriggered[2] = false;
  rt.autoRecalPending = false;
  rt.nextAutoRecalTurn = AUTO_RECAL_INTERVAL_TURNS;
  rt.chaosSpaceOffset_um = 0;
  rt.chaosTurnsRemaining = 0;
  rt.driftCompAccum = 0;
  g_prevRecalTurns = 0;

  clearActiveMid();
  if (rt.midArmed) {
    rt.midActive = true;
    rt.midInset_um = rt.midArmedInset_um;
    rt.midTurnsRemaining = rt.midArmedTurns;
    clearArmedMid();
  }

  chooseNewChaosSpacingOffset();
}

void maybeReverseTraverse() {
  if (rt.traverseDir == TraverseDir::TO_TOP) {
    if (topLimitTriggered() || atEffectiveTop()) {
      rt.traverseDir = TraverseDir::TO_BOTTOM;
      recordReversal();
    }
  } else {
    if (bottomLimitTriggered() || atEffectiveBottom()) {
      rt.traverseDir = TraverseDir::TO_TOP;
      recordReversal();
    }
  }
}

void handleTraverseDemandOnWinderStep() {
  if (st.state != MachineState::RUNNING_AUTO) return;
  if (!cal.softValid) return;

  maybeReverseTraverse();

  int32_t baseSpaceUm = getEffectiveSpaceUm();
  int32_t scaledSpace_um = (int32_t)lroundf((float)baseSpaceUm * getTraverseScale());
  if (scaledSpace_um < 0) scaledSpace_um = 0;

  rt.traverseDemandNumer += scaledSpace_um;
  const int64_t DEN = (int64_t)WINDER_STEPS_PER_REV * (int64_t)TRAV_STEP_UM;

  while (rt.traverseDemandNumer >= DEN) {
    maybeReverseTraverse();
    emitTraverseSingleStep(rt.traverseDir);
    rt.traverseDemandNumer -= DEN;
    break;
  }
}

// Turns the spindle will add while ramping from the current speed to zero:
// decel takes rpm/ramp seconds at an average of rpm/2 -> rpm^2/(120*ramp) turns.
// Used so taps and the total-turn stop land ON target instead of overshooting
// through the ramp-down (~28 turns at 500 rpm / 75 rpm/s).
uint32_t decelAnticipationTurns() {
  float r = currentWinderRPM;
  float ramp = (float)cfg.rampRPMperSec;
  if (ramp < (float)MIN_RAMP_RPM_S) ramp = (float)MIN_RAMP_RPM_S;
  float t = (r * r) / (120.0f * ramp);
  if (t < 0.0f) t = 0.0f;
  return (uint32_t)t;
}

bool checkTapTrigger() {
  if (cfg.tapCount == 0) return false;

  // Only one tap can be pending at a time. Without this, a second tap target
  // reached during the first tap's ramp-down overwrote activeTapIndex and the
  // first tap's pause was silently skipped.
  if (rt.activeTapIndex >= 0) return true;

  uint32_t antic = decelAnticipationTurns();

  for (uint8_t i = 0; i < cfg.tapCount && i < 3; ++i) {
    if (cfg.tapTargets[i] == 0 || rt.tapTriggered[i]) continue;
    if (rt.turnsDone + antic >= cfg.tapTargets[i]) {
      rt.tapTriggered[i] = true;
      rt.activeTapIndex = (int8_t)i;
      commandedWinderRPM = 0.0f;
      return true;
    }
  }
  return false;
}

// Drift compensation, applied once per completed turn while winding.
// Spreads cfg.driftCompSteps of counter correction evenly across every
// cfg.driftCompTurns turns using a fractional accumulator, so the correction is
// a single step at a time and never a jump.
void applyDriftCompOnTurn() {
  if (cfg.driftCompTurns == 0 || cfg.driftCompSteps == 0) return;

  int32_t turns = (int32_t)cfg.driftCompTurns;
  rt.driftCompAccum += cfg.driftCompSteps;

  while (rt.driftCompAccum >= turns) {
    traverse.positionSteps += 1;
    rt.driftCompAccum -= turns;
  }
  while (rt.driftCompAccum <= -turns) {
    traverse.positionSteps -= 1;
    rt.driftCompAccum += turns;
  }
}

// -----------------------------------------------------------------------------
// Snapshot helpers for profile-based recal from pause
// -----------------------------------------------------------------------------
void saveManualProfileRecalSnapshot() {
  recalSnap.valid = true;
  recalSnap.turnsDone = rt.turnsDone;
  recalSnap.stepInTurn = rt.stepInTurn;
  recalSnap.traverseDir = rt.traverseDir;
  recalSnap.tapTriggered[0] = rt.tapTriggered[0];
  recalSnap.tapTriggered[1] = rt.tapTriggered[1];
  recalSnap.tapTriggered[2] = rt.tapTriggered[2];
  recalSnap.activeTapIndex = rt.activeTapIndex;
  recalSnap.nextAutoRecalTurn = rt.nextAutoRecalTurn;

  updateEffectiveBounds();
  int32_t oldTop = rt.effectiveTopSteps;
  int32_t oldBottom = rt.effectiveBottomSteps;
  int32_t span = oldTop - oldBottom;
  recalSnap.relativePos =
      (span > 0) ? ((float)(traverse.positionSteps - oldBottom) / (float)span) : 0.0f;

  recalSnap.topInsetSteps = 0;
  recalSnap.bottomInsetSteps = 0;

  recalSnap.oldHardTopSteps = cal.hardTopSteps;
  recalSnap.oldHardBottomSteps = cal.hardBottomSteps;
  recalSnap.turnMark = rt.turnsDone;
}

void setupRestoreTargetFromCurrentEffectiveBounds() {
  updateEffectiveBounds();
  int32_t newTop = rt.effectiveTopSteps;
  int32_t newBottom = rt.effectiveBottomSteps;
  int32_t span = newTop - newBottom;

  recalRestoreTargetPos =
      newBottom + (int32_t)lroundf(recalSnap.relativePos * (float)span);

  recalRestoreTargetPos = clampI32(recalRestoreTargetPos, newBottom, newTop);
}

// -----------------------------------------------------------------------------
// Auto recal snapshot
// -----------------------------------------------------------------------------
void saveRecalSnapshot() {
  recalSnap.valid = true;
  recalSnap.turnsDone = rt.turnsDone;
  recalSnap.stepInTurn = rt.stepInTurn;
  recalSnap.traverseDir = rt.traverseDir;
  recalSnap.tapTriggered[0] = rt.tapTriggered[0];
  recalSnap.tapTriggered[1] = rt.tapTriggered[1];
  recalSnap.tapTriggered[2] = rt.tapTriggered[2];
  recalSnap.activeTapIndex = rt.activeTapIndex;
  recalSnap.nextAutoRecalTurn = rt.turnsDone + AUTO_RECAL_INTERVAL_TURNS;

  updateEffectiveBounds();
  int32_t oldTop = rt.effectiveTopSteps;
  int32_t oldBottom = rt.effectiveBottomSteps;
  int32_t span = oldTop - oldBottom;
  recalSnap.relativePos =
      (span > 0) ? ((float)(traverse.positionSteps - oldBottom) / (float)span) : 0.0f;

  recalSnap.topInsetSteps = cal.hardTopSteps - cal.softTopSteps;
  recalSnap.bottomInsetSteps = cal.softBottomSteps - cal.hardBottomSteps;

  recalSnap.oldHardTopSteps = cal.hardTopSteps;
  recalSnap.oldHardBottomSteps = cal.hardBottomSteps;
  recalSnap.turnMark = rt.turnsDone;
}

// Prints how far the traverse counter had drifted from the physical switches since
// the previous calibration. The switches do not move, and positionSteps counts
// continuously through a recal, so the re-found hard positions differ from the old
// ones by exactly the counter error accumulated in between.
//
//   correction = old - new  (positive when the carriage physically walked toward TOP
//                            and the counter fell behind, the direction driftcomp
//                            corrects with a positive value)
void printRecalDrift() {
  if (!recalSnap.valid) return;

  int32_t corrBottom = recalSnap.oldHardBottomSteps - cal.hardBottomSteps;
  int32_t corrTop    = recalSnap.oldHardTopSteps    - cal.hardTopSteps;
  int32_t corrAvg    = (corrBottom + corrTop) / 2;
  int32_t spanOld    = recalSnap.oldHardTopSteps - recalSnap.oldHardBottomSteps;
  int32_t spanNew    = cal.hardTopSteps - cal.hardBottomSteps;
  uint32_t interval  = (recalSnap.turnMark >= g_prevRecalTurns) ? (recalSnap.turnMark - g_prevRecalTurns) : 0;
  g_prevRecalTurns   = recalSnap.turnMark;

  Serial.print("RECAL_DRIFT correction_bottom_steps=");
  Serial.print(corrBottom);
  Serial.print(" correction_top_steps=");
  Serial.print(corrTop);
  Serial.print(" avg_steps=");
  Serial.print(corrAvg);
  Serial.print(" avg_mm=");
  Serial.print(stepsToMm(corrAvg), 3);
  Serial.print(" span_change_steps=");
  Serial.print(spanNew - spanOld);
  Serial.print(" over_turns=");
  Serial.print(interval);
  Serial.print(" at_turn=");
  Serial.println(recalSnap.turnMark);

  if (interval > 0) {
    Serial.print("RECAL_DRIFT suggestion: driftcomp ");
    Serial.print(corrAvg);
    Serial.print(" ");
    Serial.print(interval);
    Serial.println("   (positive = wind drifting toward TOP; bottom/top should agree, span_change is noise)");
  }
}

void restoreAfterAutoRecalSetup() {
  printRecalDrift();

  cal.softTopSteps = cal.hardTopSteps - recalSnap.topInsetSteps;
  cal.softBottomSteps = cal.hardBottomSteps + recalSnap.bottomInsetSteps;
  cal.hardValid = true;
  cal.softValid = true;
  cal.softTopSaved = true;
  cal.softBottomSaved = true;
  updateEffectiveBounds();
  setupRestoreTargetFromCurrentEffectiveBounds();
}

void finishAutoRecalResume() {
  if (!recalSnap.valid) return;

  rt.turnsDone = recalSnap.turnsDone;
  rt.stepInTurn = recalSnap.stepInTurn;
  rt.traverseDir = recalSnap.traverseDir;
  rt.tapTriggered[0] = recalSnap.tapTriggered[0];
  rt.tapTriggered[1] = recalSnap.tapTriggered[1];
  rt.tapTriggered[2] = recalSnap.tapTriggered[2];
  rt.activeTapIndex = recalSnap.activeTapIndex;
  rt.autoRecalPending = false;
  rt.nextAutoRecalTurn = recalSnap.nextAutoRecalTurn;
  recalSnap.valid = false;

  prepareTensionForRunStart();
  seedChaosRng();
  winder.forceDir(cfg.winderCW ^ WINDER_DIR_INVERT);
  beginWinderWithVisibleFloor(cfg.rpm);
  enterState(MachineState::RUNNING_AUTO);
  Serial.println("OK auto recal complete, resumed");
}

// -----------------------------------------------------------------------------
// Calibration
// -----------------------------------------------------------------------------
bool prepareCalibrationReturnState() {
  calReturnState = MachineState::READY;
  calNeedsRestoreProgress = false;

  if (st.state == MachineState::PAUSED_AUTO || st.state == MachineState::TAP_PAUSE) {
    calReturnState = st.state;
    calNeedsRestoreProgress = true;
    saveManualProfileRecalSnapshot();
    return true;
  }

  if (st.state == MachineState::READY ||
      st.state == MachineState::IDLE ||
      st.state == MachineState::TERMINATED ||
      st.state == MachineState::CAL_JOG) {
    calReturnState = MachineState::READY;
    calNeedsRestoreProgress = false;
    recalSnap.valid = false;
    return true;
  }

  return false;
}

void startCalibration(CalMode mode) {
  if (!requiredSetupComplete()) {
    Serial.println("ERR missing required setup");
    return;
  }

  if (mode == CalMode::AUTO_RECAL) {
    if (st.state != MachineState::RUNNING_AUTO || st.currentRPM != 0) {
      Serial.println("ERR auto recal start invalid");
      return;
    }
  } else {
    if (!prepareCalibrationReturnState()) {
      Serial.println("ERR cannot calibrate in current state");
      return;
    }
  }

  cal.hardValid = false;
  cal.softValid = false;
  calMode = mode;
  jogDir = 0;
  calFindSub = CalFindSubstate::GO_BOTTOM_FAST;
  bottomBackoffArmed = false;
  topBackoffArmed = false;
  startPhase = StartPhase::NONE;
  rt.autoStartPending = false;

  stopTraverse();

  enterState(MachineState::CAL_FIND_LIMITS);

  seedChaosRng();

  if (mode == CalMode::PROFILE_TEMPLATE) Serial.println("OK profile calibration started");
  else if (mode == CalMode::AUTO_RECAL) Serial.println("OK auto recal started");
  else Serial.println("OK calibration started");
}

void calibrationHardFault(const char* reason) {
  hardStopNow();
  calFindSub = CalFindSubstate::DONE;
  cal.hardValid = false;
  cal.softValid = false;
  enterState(MachineState::IDLE);
  Serial.print("ERR calibration aborted: ");
  Serial.println(reason);
  Serial.println("Check traverse direction, top/bottom switch wiring, and switch polarity before running cal again.");
}

void updateCalibrationFind() {
  switch (calFindSub) {
    case CalFindSubstate::GO_BOTTOM_FAST:
      if (topLimitTriggered() && !bottomLimitTriggered()) {
        calibrationHardFault("TOP switch triggered while seeking BOTTOM fast");
      } else if (bottomLimitTriggered()) {
        stopTraverse();
        bottomBackoffArmed = false;
        calFindSub = CalFindSubstate::BACKOFF_BOTTOM;
      } else {
        setTraverseRPM(CAL_FAST_RPM, TraverseDir::TO_BOTTOM);
      }
      break;

    case CalFindSubstate::BACKOFF_BOTTOM:
      if (topLimitTriggered() && !bottomLimitTriggered()) {
        calibrationHardFault("TOP switch triggered while backing off BOTTOM");
        break;
      }
      if (!bottomBackoffArmed) {
        bottomBackoffStart = traverse.positionSteps;
        bottomBackoffArmed = true;
      }
      if (bottomLimitTriggered()) {
        setTraverseRPM(CAL_FAST_RPM, TraverseDir::TO_TOP);
      } else {
        int32_t moved_um = (traverse.positionSteps - bottomBackoffStart) * TRAV_STEP_UM;
        if (moved_um >= CAL_BACKOFF_UM) {
          stopTraverse();
          bottomBackoffArmed = false;
          calFindSub = CalFindSubstate::GO_BOTTOM_SLOW;
        } else {
          setTraverseRPM(CAL_FAST_RPM, TraverseDir::TO_TOP);
        }
      }
      break;

    case CalFindSubstate::GO_BOTTOM_SLOW:
      if (topLimitTriggered() && !bottomLimitTriggered()) {
        calibrationHardFault("TOP switch triggered while seeking BOTTOM slow");
      } else if (bottomLimitTriggered()) {
        stopTraverse();
        cal.hardBottomSteps = traverse.positionSteps;
        bottomBackoffArmed = false;
        calFindSub = CalFindSubstate::CLEAR_BOTTOM_AFTER_SLOW;
      } else {
        setTraverseRPM(CAL_SLOW_RPM, TraverseDir::TO_BOTTOM);
      }
      break;

    case CalFindSubstate::CLEAR_BOTTOM_AFTER_SLOW:
      if (topLimitTriggered() && !bottomLimitTriggered()) {
        calibrationHardFault("TOP switch triggered while clearing BOTTOM after slow hit");
      } else if (bottomLimitTriggered()) {
        setTraverseRPM(CAL_SLOW_RPM, TraverseDir::TO_TOP);
      } else {
        stopTraverse();
        calFindSub = CalFindSubstate::GO_TOP_FAST;
      }
      break;

    case CalFindSubstate::GO_TOP_FAST:
      if (bottomLimitTriggered() && !topLimitTriggered()) {
        calibrationHardFault("BOTTOM switch triggered while seeking TOP fast");
      } else if (topLimitTriggered()) {
        stopTraverse();
        topBackoffArmed = false;
        calFindSub = CalFindSubstate::BACKOFF_TOP;
      } else {
        setTraverseRPM(CAL_FAST_RPM, TraverseDir::TO_TOP);
      }
      break;

    case CalFindSubstate::BACKOFF_TOP:
      if (bottomLimitTriggered() && !topLimitTriggered()) {
        calibrationHardFault("BOTTOM switch triggered while backing off TOP");
        break;
      }
      if (!topBackoffArmed) {
        topBackoffStart = traverse.positionSteps;
        topBackoffArmed = true;
      }
      if (topLimitTriggered()) {
        setTraverseRPM(CAL_FAST_RPM, TraverseDir::TO_BOTTOM);
      } else {
        int32_t moved_um = (topBackoffStart - traverse.positionSteps) * TRAV_STEP_UM;
        if (moved_um >= CAL_BACKOFF_UM) {
          stopTraverse();
          topBackoffArmed = false;
          calFindSub = CalFindSubstate::GO_TOP_SLOW;
        } else {
          setTraverseRPM(CAL_FAST_RPM, TraverseDir::TO_BOTTOM);
        }
      }
      break;

    case CalFindSubstate::GO_TOP_SLOW:
      if (bottomLimitTriggered() && !topLimitTriggered()) {
        calibrationHardFault("BOTTOM switch triggered while seeking TOP slow");
      } else if (topLimitTriggered()) {
        stopTraverse();
        cal.hardTopSteps = traverse.positionSteps;
        topBackoffArmed = false;
        calFindSub = CalFindSubstate::CLEAR_TOP_AFTER_SLOW;
      } else {
        setTraverseRPM(CAL_SLOW_RPM, TraverseDir::TO_TOP);
      }
      break;

    case CalFindSubstate::CLEAR_TOP_AFTER_SLOW:
      if (bottomLimitTriggered() && !topLimitTriggered()) {
        calibrationHardFault("BOTTOM switch triggered while clearing TOP after slow hit");
      } else if (topLimitTriggered()) {
        setTraverseRPM(CAL_SLOW_RPM, TraverseDir::TO_BOTTOM);
      } else {
        stopTraverse();
        if (calMode == CalMode::AUTO_RECAL) {
          restoreAfterAutoRecalSetup();
          calFindSub = CalFindSubstate::RESTORE_PROGRESS;
        } else if (calMode == CalMode::PROFILE_TEMPLATE) {
          cal.hardValid = true;
          if (!applyStoredCalProfileToCurrentHardBounds()) {
            enterState(MachineState::IDLE);
            calFindSub = CalFindSubstate::DONE;
          } else if (calNeedsRestoreProgress && recalSnap.valid) {
            printRecalDrift();
            setupRestoreTargetFromCurrentEffectiveBounds();
            calFindSub = CalFindSubstate::RESTORE_PROGRESS;
          } else {
            calFindSub = CalFindSubstate::DONE;
          }
        } else {
          calFindSub = CalFindSubstate::GO_CENTER;
        }
      }
      break;

    case CalFindSubstate::GO_CENTER: {
      int32_t center = (cal.hardTopSteps + cal.hardBottomSteps) / 2;
      if (traverse.positionSteps < center) {
        if (topLimitTriggered()) calibrationHardFault("TOP switch triggered while moving to center");
        else setTraverseRPM(CAL_CENTER_RPM, TraverseDir::TO_TOP);
      } else if (traverse.positionSteps > center) {
        if (bottomLimitTriggered()) calibrationHardFault("BOTTOM switch triggered while moving to center");
        else setTraverseRPM(CAL_CENTER_RPM, TraverseDir::TO_BOTTOM);
      } else {
        stopTraverse();
        calFindSub = CalFindSubstate::DONE;
      }
      break;
    }

    case CalFindSubstate::RESTORE_PROGRESS:
      if (traverse.positionSteps < recalRestoreTargetPos) {
        if (topLimitTriggered()) calibrationHardFault("TOP switch triggered while restoring position");
        else setTraverseRPM(CAL_CENTER_RPM, TraverseDir::TO_TOP);
      } else if (traverse.positionSteps > recalRestoreTargetPos) {
        if (bottomLimitTriggered()) calibrationHardFault("BOTTOM switch triggered while restoring position");
        else setTraverseRPM(CAL_CENTER_RPM, TraverseDir::TO_BOTTOM);
      } else {
        stopTraverse();
        if (calMode == CalMode::AUTO_RECAL) {
          finishAutoRecalResume();
        } else {
          enterState(calReturnState);
          Serial.print("OK profile calibration complete, state=");
          Serial.println(stateName(calReturnState));
          recalSnap.valid = false;
          calNeedsRestoreProgress = false;
        }
        calFindSub = CalFindSubstate::DONE;
      }
      break;

    case CalFindSubstate::DONE:
      stopTraverse();
      if (calMode == CalMode::MANUAL) {
        cal.hardValid = true;
        enterState(MachineState::CAL_JOG);
        Serial.println("OK hard limits found");
        Serial.println("Use jogt/nudget to set TOP work bound, then savet");
        Serial.println("Use jogb/nudgeb to set BOTTOM work bound, then saveb");
      } else if (calMode == CalMode::PROFILE_TEMPLATE && !calNeedsRestoreProgress) {
        enterState(calReturnState);
        Serial.print("OK profile calibration complete, state=");
        Serial.println(stateName(calReturnState));
      }
      break;
  }
}

// -----------------------------------------------------------------------------
// Run control
// -----------------------------------------------------------------------------
void prepareTensionForRunStart() {
  // Manual tension setup only. Use zero with no reference mass, then tare <grams>
  // with the known reference mass applied. Auto run, defcal, manual cal, and
  // auto recal do not change tension.zero or the reference-mass calibration.
  resetTensionDropFaultMonitor();
}

void startAutoRun() {
  if (!requiredSetupComplete()) {
    Serial.println("ERR missing required setup");
    return;
  }
  if (!cal.softValid) {
    Serial.println("ERR calibration required");
    return;
  }
  if (!permitOK()) {
    Serial.println("ERR permit fault");
    return;
  }
  if (!snagOK()) {
    Serial.println("ERR snag fault");
    return;
  }

  if (st.state == MachineState::RUNNING_AUTO ||
      st.state == MachineState::RUNNING_SPIN ||
      st.state == MachineState::PAUSED_AUTO ||
      st.state == MachineState::TAP_PAUSE ||
      st.state == MachineState::CAL_FIND_LIMITS ||
      st.state == MachineState::CAL_JOG ||
      st.state == MachineState::FAULT_FAILSAFE ||
      st.state == MachineState::FAULT_SNAG ||
      st.state == MachineState::FAULT_TENSION) {
    Serial.println("ERR cannot start in current state");
    return;
  }

  normalizeTapConfig(true);

  if (cfg.tapCount > 0) {
    for (uint8_t i = 0; i < cfg.tapCount; ++i) {
      if (cfg.tapTargets[i] == 0) {
        Serial.println("ERR tap target invalid");
        return;
      }
      if (cfg.tapTargets[i] >= cfg.totalTurns) {
        Serial.println("ERR tap target must be below total turns");
        return;
      }
      if (i > 0 && cfg.tapTargets[i] <= cfg.tapTargets[i - 1]) {
        Serial.println("ERR tap targets must be ascending");
        return;
      }
    }
  }

  updateEffectiveBounds();
  resetAutoProgress();
  seedChaosRng();

  if (!bottomLimitTriggered() && !atEffectiveBottom()) {
    startPhase = StartPhase::MOVE_TO_EFFECTIVE_BOTTOM;
    rt.autoStartPending = true;
    enterState(MachineState::READY);
    Serial.println("OK moving to bottom start position");
    return;
  }

  prepareTensionForRunStart();
  winder.forceDir(cfg.winderCW ^ WINDER_DIR_INVERT);
  beginWinderWithVisibleFloor(cfg.rpm);
  enterState(MachineState::RUNNING_AUTO);
  statusTimer = 0;
  Serial.println("OK auto run started");
}

void pauseAutoRun() {
  if (st.state != MachineState::RUNNING_AUTO) {
    Serial.println("ERR not in auto run");
    return;
  }

  rt.pauseRequested = true;
  commandedWinderRPM = 0.0f;
  Serial.println("OK pause requested");
}

void resumeRun() {
  if (st.state == MachineState::FAULT_FAILSAFE ||
      st.state == MachineState::FAULT_SNAG ||
      st.state == MachineState::FAULT_TENSION) {
    Serial.println("ERR fault active");
    Serial.println("Clear physical fault first, then send ack");
    return;
  }

  if (st.state != MachineState::PAUSED_AUTO && st.state != MachineState::TAP_PAUSE) {
    Serial.println("ERR not paused");
    return;
  }
  if (!permitOK()) {
    Serial.println("ERR permit fault");
    return;
  }
  if (!snagOK()) {
    Serial.println("ERR snag fault");
    return;
  }

  // Clear any stale intent from before the pause/tap/fault. Without this, a
  // stop sent during a pause decel silently terminated the run at the next
  // rpm=0 moment (e.g. a tap).
  rt.stopRequested = false;
  rt.pauseRequested = false;
  rt.activeTapIndex = -1;

  // A recal that was pending when a tap/pause intervened would otherwise stay
  // "pending" forever (the trigger only fires when not pending). Clearing it
  // lets it re-arm on the next turn, since the turn threshold is already met.
  rt.autoRecalPending = false;

  prepareTensionForRunStart();
  seedChaosRng();
  winder.forceDir(cfg.winderCW ^ WINDER_DIR_INVERT);
  beginWinderWithVisibleFloor(cfg.rpm);
  enterState(MachineState::RUNNING_AUTO);
  statusTimer = 0;
  Serial.println("OK resumed");
}

void startSpin(uint32_t rpm, bool cw) {
  if (!permitOK()) {
    Serial.println("ERR permit fault");
    return;
  }
  if (!snagOK()) {
    Serial.println("ERR snag fault");
    return;
  }
  if (rpm < MIN_RPM || rpm > MAX_RPM) {
    Serial.println("ERR spin rpm out of range");
    return;
  }

  if (st.state == MachineState::RUNNING_AUTO ||
      st.state == MachineState::RUNNING_SPIN ||
      st.state == MachineState::PAUSED_AUTO ||
      st.state == MachineState::TAP_PAUSE ||
      st.state == MachineState::CAL_FIND_LIMITS ||
      st.state == MachineState::CAL_JOG ||
      st.state == MachineState::FAULT_FAILSAFE ||
      st.state == MachineState::FAULT_SNAG ||
      st.state == MachineState::FAULT_TENSION) {
    Serial.println("ERR cannot spin in current state");
    return;
  }

  stopTraverse();
  winder.forceDir(cw ^ WINDER_DIR_INVERT);
  delayMicroseconds(DIR_SETUP_US);
  // Ramp to speed like an auto run does. Commanding the full step rate in one
  // jump (and re-applying it every loop pass) stalled the motor at high rpm.
  beginWinderWithVisibleFloor(rpm);
  rt.spinRPM = rpm;
  rt.spinCW = cw;
  enterState(MachineState::RUNNING_SPIN);

  Serial.print("OK spin ");
  Serial.print(rpm);
  Serial.print(" ");
  Serial.println(dirName(cw));
}

// -----------------------------------------------------------------------------
// Fault / stop / reset / status / help
// -----------------------------------------------------------------------------
void printFaultSnapshot(const char* reason) {
  Serial.print("FAULTSNAP reason=");
  Serial.print(reason);
  Serial.print(" state=");
  Serial.print(stateName(st.state));
  Serial.print(" turns=");
  Serial.print(rt.turnsDone);
  Serial.print(" stepinturn=");
  Serial.print(rt.stepInTurn);
  Serial.print(" trav_steps=");
  Serial.print(traverse.positionSteps);
  Serial.print(" trav_mm=");
  Serial.print(stepsToMm(traverse.positionSteps), 3);
  Serial.print(" trav_dir=");
  Serial.print(travDirName(rt.traverseDir));
  Serial.print(" rpm_now=");
  Serial.print(st.currentRPM);
  Serial.print(" rpm_target=");
  Serial.print(commandedWinderRPM, 0);
  Serial.print(" permit=");
  Serial.print(st.permitOK ? 1 : 0);
  Serial.print(" snag=");
  Serial.print(st.snagOK ? 1 : 0);
  Serial.print(" top_lim=");
  Serial.print(st.topLimit ? 1 : 0);
  Serial.print(" bottom_lim=");
  Serial.print(st.bottomLimit ? 1 : 0);
  Serial.print(tension.calibrated ? " tension_g=" : " tension_raw=");
  Serial.print(tension.force);
  Serial.print(" tension_base_g=");
  Serial.print(tension.dropBaseline_g);
  Serial.print(" tension_thresh_g=");
  Serial.print(tension.dropThreshold_g);
  Serial.print(" tension_low_turns=");
  Serial.println(tension.dropConfirmedTurns);
}

void printFaultInstructions(MachineState s) {
  Serial.println();
  if (s == MachineState::FAULT_FAILSAFE) {
    Serial.println("FAILSAFE FAULT:");
    Serial.println("  1) Restore pin 33 to run-enabled");
    Serial.println("  2) Send: ack");
    Serial.println("  3) Then send: resume to continue, or stop to terminate");
  } else if (s == MachineState::FAULT_SNAG) {
    Serial.println("SNAG FAULT:");
    Serial.println("  1) Clear physical snag condition");
    Serial.println("  2) Send: ack");
    Serial.println("  3) Then send: resume to continue, or stop to terminate");
  } else if (s == MachineState::FAULT_TENSION) {
    Serial.println("TENSION DROP FAULT:");
    Serial.println("  1) Check for wire break, winder stall, or loss of spindle motion");
    Serial.println("  2) Send: ack");
    Serial.println("  3) Then send: resume to continue, or stop to terminate");
  }
  Serial.println();
}

void enterTensionDropFault() {
  if (st.state != MachineState::RUNNING_AUTO) return;

  faultReturnState = st.state;
  faultResumeAvailable = true;
  hardStopNow();
  enterState(MachineState::FAULT_TENSION);
  printFaultSnapshot("tension_drop");
  printFaultInstructions(MachineState::FAULT_TENSION);
}

bool inFaultState() {
  return st.state == MachineState::FAULT_FAILSAFE ||
         st.state == MachineState::FAULT_SNAG ||
         st.state == MachineState::FAULT_TENSION;
}

void handleFaults() {
  if (!permitOK()) {
    if (st.state != MachineState::FAULT_FAILSAFE) {
      // A second fault while already faulted must not overwrite the saved
      // resume context, or the run becomes unrecoverable after ack.
      if (!inFaultState()) {
        faultReturnState = st.state;
        faultResumeAvailable =
            (st.state == MachineState::RUNNING_AUTO ||
             st.state == MachineState::PAUSED_AUTO ||
             st.state == MachineState::TAP_PAUSE);
      }
      hardStopNow();
      enterState(MachineState::FAULT_FAILSAFE);
      printFaultSnapshot("permit");
      printFaultInstructions(MachineState::FAULT_FAILSAFE);
    }
    return;
  }

  if (!snagOK()) {
    if (st.state == MachineState::RUNNING_AUTO ||
        st.state == MachineState::RUNNING_SPIN ||
        st.state == MachineState::PAUSED_AUTO ||
        st.state == MachineState::TAP_PAUSE ||
        st.state == MachineState::CAL_FIND_LIMITS ||
        st.state == MachineState::CAL_JOG) {
      faultReturnState = st.state;
      faultResumeAvailable =
          (st.state == MachineState::RUNNING_AUTO ||
           st.state == MachineState::PAUSED_AUTO ||
           st.state == MachineState::TAP_PAUSE);
      hardStopNow();
      enterState(MachineState::FAULT_SNAG);
      printFaultSnapshot("snag");
      printFaultInstructions(MachineState::FAULT_SNAG);
    }
  }
}

void stopAction() {
  if (st.state == MachineState::CAL_JOG) {
    stopTraverse();
    jogDir = 0;
    Serial.println("OK jog stopped");
    return;
  }

  if (st.state == MachineState::CAL_FIND_LIMITS) {
    stopTraverse();
    enterState(MachineState::IDLE);
    Serial.println("OK calibration stopped");
    return;
  }

  if (st.state == MachineState::RUNNING_AUTO) {
    rt.stopRequested = true;
    commandedWinderRPM = 0.0f;
    Serial.println("OK auto stop requested");
    return;
  }

  if (st.state == MachineState::RUNNING_SPIN) {
    hardStopNow();
    enterState(MachineState::TERMINATED);
    Serial.println("OK spin stopped");
    return;
  }

  if (st.state == MachineState::PAUSED_AUTO || st.state == MachineState::TAP_PAUSE) {
    hardStopNow();
    enterState(MachineState::TERMINATED);
    Serial.println("OK stopped");
    return;
  }

  Serial.println("OK");
}

// Full reset. keepTensionCal = true preserves the HX711 zero + tare calibration
// (the "reset" command) so the reference mass does not have to be re-hung.
void doReset(bool keepTensionCal) {
  bool  keepCalibrated    = tension.calibrated;
  long  keepZero          = tension.zero;
  long  keepCalRaw        = tension.calRaw;
  float keepRefGrams      = tension.refGrams;
  float keepCountsPerGram = tension.countsPerGram;

  hardStopNow();

  cfg = Config();
  setupFlags = SetupFlags();
  applyStandardDefaults();
  applyStandardDefaultProfile();
  cal = Calibration();
  rt = RuntimeData();
  st = LiveStatus();
  recalSnap = RecalSnapshot();
  calProfile = CalProfile();
  chaosEntropy = ChaosEntropyInfo();
  tension = TensionData();

  if (keepTensionCal) {
    tension.calibrated    = keepCalibrated;
    tension.zero          = keepZero;
    tension.calRaw        = keepCalRaw;
    tension.refGrams      = keepRefGrams;
    tension.countsPerGram = keepCountsPerGram;
  }

  calMode = CalMode::MANUAL;
  calFindSub = CalFindSubstate::GO_BOTTOM_FAST;
  startPhase = StartPhase::NONE;

  bottomBackoffArmed = false;
  topBackoffArmed = false;
  bottomBackoffStart = 0;
  topBackoffStart = 0;
  recalRestoreTargetPos = 0;

  faultReturnState = MachineState::IDLE;
  faultResumeAvailable = false;
  calReturnState = MachineState::READY;
  calNeedsRestoreProgress = false;

  chaosRngState = 0x6D2B79F5u;
  g_prevRecalTurns = 0;
  jogDir = 0;

  rxLine = "";
  rampDtUs = 0;
  statusTimer = 0;

  winder.begin(PIN_WINDER_STEP, PIN_WINDER_DIR, false ^ WINDER_DIR_INVERT, WINDER_DIR_INVERT);
  traverse.begin(PIN_TRAV_STEP, PIN_TRAV_DIR, false ^ TRAVERSE_DIR_INVERT, TRAVERSE_DIR_INVERT);
  hx711Begin();
  tensionResetWindow();
  resetTensionDropFaultMonitor();

  refreshInputs();
  seedChaosRng();
  enterState(MachineState::IDLE);
}

void resetAll() {
  doReset(false);
  Serial.println("OK resetall complete");
}

void resetKeepTension() {
  doReset(true);
  Serial.print("OK reset complete");
  Serial.println(tension.calibrated ? " (tension zero + tare kept)" : " (no tension calibration was stored)");
}

void printRunStatusLine() {
  updateEffectiveBounds();
  int32_t actTop = 0;
  int32_t actBottom = 0;
  if (cal.softValid) getActiveBounds(actTop, actBottom);

  Serial.print("STATUS state=");
  Serial.print(stateName(st.state));
  Serial.print(" turns=");
  Serial.print(rt.turnsDone);
  Serial.print(" remaining=");
  Serial.print((cfg.totalTurns > rt.turnsDone) ? (cfg.totalTurns - rt.turnsDone) : 0);
  Serial.print(" total=");
  Serial.print(cfg.totalTurns);
  Serial.print(" rpm_now=");
  Serial.print(st.currentRPM);
  Serial.print(" rpm_target=");
  Serial.print(commandedWinderRPM, 0);
  Serial.print(" trav_steps=");
  Serial.print(traverse.positionSteps);
  Serial.print(" trav_mm=");
  Serial.print(stepsToMm(traverse.positionSteps), 3);
  Serial.print(" trav_dir=");
  Serial.print(travDirName(rt.traverseDir));
  Serial.print(" act_top=");
  Serial.print(actTop);
  Serial.print(" act_bot=");
  Serial.print(actBottom);
  Serial.print(" mid_active=");
  Serial.print(rt.midActive ? 1 : 0);
  Serial.print(" mid_left=");
  Serial.print(rt.midTurnsRemaining);
  Serial.print(" chaos_um=");
  Serial.print(rt.chaosSpaceOffset_um);
  Serial.print(" chaos_turns_left=");
  Serial.print(rt.chaosTurnsRemaining);
  Serial.print(" permit=");
  Serial.print(st.permitOK ? 1 : 0);
  Serial.print(" snag=");
  Serial.print(st.snagOK ? 1 : 0);
  Serial.print(" top_lim=");
  Serial.print(st.topLimit ? 1 : 0);
  Serial.print(" bottom_lim=");
  Serial.print(st.bottomLimit ? 1 : 0);
  Serial.print(tension.calibrated ? " tension_g=" : " tension_raw=");
  Serial.print(tension.force);
  Serial.print(" tension_base_g=");
  Serial.print(tension.dropBaseline_g);
  Serial.print(" tension_thresh_g=");
  Serial.print(tension.dropThreshold_g);
  Serial.print(" tension_low_turns=");
  Serial.println(tension.dropConfirmedTurns);
}

void printStatus() {
  updateEffectiveBounds();
  int32_t activeTop = 0;
  int32_t activeBottom = 0;
  if (cal.softValid) getActiveBounds(activeTop, activeBottom);

  Serial.println();
  Serial.println("=== MACHINE STATUS ===");
  Serial.print("STATE: "); Serial.println(stateName(st.state));
  Serial.print("PERMIT: "); Serial.println(st.permitOK ? "OK" : "FAULT");
  Serial.print("SNAG: "); Serial.println(st.snagOK ? "OK" : "FAULT");
  Serial.print("TOP_LIMIT: "); Serial.println(st.topLimit ? 1 : 0);
  Serial.print("BOTTOM_LIMIT: "); Serial.println(st.bottomLimit ? 1 : 0);
  Serial.print("TOP_LIMIT_RAW: "); Serial.println(topLimitRaw() ? 1 : 0);
  Serial.print("BOTTOM_LIMIT_RAW: "); Serial.println(bottomLimitRaw() ? 1 : 0);

  Serial.println();
  Serial.println("=== SETTINGS ===");
  Serial.print("DIR: "); Serial.println(dirName(cfg.winderCW));
  Serial.print("RPM: "); Serial.println(cfg.rpm);
  Serial.print("CURRENT_RPM: "); Serial.println(st.currentRPM);
  Serial.print("RAMP_RPM_PER_SEC: "); Serial.println(cfg.rampRPMperSec);
  Serial.print("TARGET_TURNS: "); Serial.println(cfg.totalTurns);
  Serial.print("SPACE_MM: "); Serial.println(umToMm(cfg.space_um), 3);
  Serial.print("WINDER_STEPS_PER_REV: "); Serial.println(WINDER_STEPS_PER_REV);
  Serial.print("TRAV_STEPS_PER_REV: "); Serial.println(TRAV_STEPS_PER_REV);
  Serial.print("TRAV_LEAD_MM_PER_REV: "); Serial.println(umToMm(TRAV_LEAD_UM_PER_REV), 3);
  Serial.print("TRAV_STEP_MM: "); Serial.println(umToMm(TRAV_STEP_UM), 3);
  Serial.print("CAL_FAST_RPM: "); Serial.println(CAL_FAST_RPM);
  Serial.print("CAL_SLOW_RPM: "); Serial.println(CAL_SLOW_RPM);
  Serial.print("CAL_CENTER_RPM: "); Serial.println(CAL_CENTER_RPM);
  Serial.print("JOG_RPM: "); Serial.println(JOG_RPM);
  Serial.print("NUDGE_STEP_DELAY_US: "); Serial.println(NUDGE_STEP_DELAY_US);
  Serial.print("OFFTOP_MM: "); Serial.println(umToMm(cfg.offTop_um), 3);
  Serial.print("OFFBOTTOM_MM: "); Serial.println(umToMm(cfg.offBottom_um), 3);
  Serial.print("SOFTDIST_MM: "); Serial.println(umToMm(cfg.softDist_um), 3);
  Serial.print("SOFTPCT: "); Serial.println(cfg.softPct);
  Serial.print("CHAOS_ENABLE: "); Serial.println(cfg.chaosEnable ? 1 : 0);
  Serial.print("CHAOS_PCT: "); Serial.println(cfg.chaosPct);
  Serial.print("CHAOS_PERSIST_TURNS: "); Serial.println(cfg.chaosPersistTurns);
  Serial.print("DRIFTCOMP_STEPS: "); Serial.println(cfg.driftCompSteps);
  Serial.print("DRIFTCOMP_TURNS: "); Serial.println(cfg.driftCompTurns);

  Serial.println();
  Serial.println("=== MID FUNCTION ===");
  Serial.print("MID_ACTIVE: "); Serial.println(rt.midActive ? 1 : 0);
  Serial.print("MID_INSET_MM: "); Serial.println(umToMm(rt.midInset_um), 3);
  Serial.print("MID_TURNS_REMAINING: "); Serial.println(rt.midTurnsRemaining);
  Serial.print("MID_ARMED: "); Serial.println(rt.midArmed ? 1 : 0);
  Serial.print("MID_ARMED_INSET_MM: "); Serial.println(umToMm(rt.midArmedInset_um), 3);
  Serial.print("MID_ARMED_TURNS: "); Serial.println(rt.midArmedTurns);

  Serial.println();
  Serial.println("=== RUNTIME ===");
  Serial.print("TURNS_DONE: "); Serial.println(rt.turnsDone);
  Serial.print("STEP_IN_TURN: "); Serial.println(rt.stepInTurn);
  Serial.print("TRAV_POS_STEPS: "); Serial.println(traverse.positionSteps);
  Serial.print("TRAV_POS_MM: "); Serial.println(stepsToMm(traverse.positionSteps), 3);
  Serial.print("TRAV_DIR: "); Serial.println(travDirName(rt.traverseDir));
  Serial.print("CHAOS_SPACE_OFFSET_UM: "); Serial.println(rt.chaosSpaceOffset_um);
  Serial.print("CHAOS_TURNS_REMAINING: "); Serial.println(rt.chaosTurnsRemaining);
  Serial.print("EFFECTIVE_SPACE_MM: "); Serial.println(umToMm(getEffectiveSpaceUm()), 3);
  Serial.print("DRIFTCOMP_ACCUM: "); Serial.println(rt.driftCompAccum);

  Serial.println();
  Serial.println("=== TENSION ===");
  Serial.print("TENSION_ENABLED: " ); Serial.println(tension.enabled ? 1 : 0);
  Serial.print("TENSION_PLOT: " ); Serial.println(tension.plotEnabled ? 1 : 0);
  Serial.print("HX711_READY_NOW: " ); Serial.println(hx711Ready() ? 1 : 0);
  Serial.print("TENSION_RAW: " ); Serial.println(tension.raw);
  Serial.print("TENSION_ZERO_RAW: " ); Serial.println(tension.zero);
  Serial.print("TENSION_CALIBRATED: " ); Serial.println(tension.calibrated ? 1 : 0);
  Serial.print("TENSION_REFERENCE_GRAMS: " ); Serial.println(tension.refGrams, 3);
  Serial.print("TENSION_REFERENCE_RAW: " ); Serial.println(tension.calRaw);
  Serial.print("TENSION_COUNTS_PER_GRAM: " ); Serial.println(tension.countsPerGram, 6);
  Serial.print(tension.calibrated ? "TENSION_FORCE_G: " : "TENSION_FORCE_RAW_DELTA: " ); Serial.println(tension.force);
  Serial.print(tension.calibrated ? "TENSION_MIN_1S_G: " : "TENSION_MIN_1S_RAW: " ); Serial.println(tension.lastWindowMin);
  Serial.print(tension.calibrated ? "TENSION_MAX_1S_G: " : "TENSION_MAX_1S_RAW: " ); Serial.println(tension.lastWindowMax);
  Serial.print(tension.calibrated ? "TENSION_AVG_3S_G: " : "TENSION_AVG_3S_RAW: " ); Serial.println(tension.lastAvg3s);
  Serial.print("TENSION_DROP_FAULT_ENABLED: "); Serial.println(tension.dropFaultEnabled ? 1 : 0);
  Serial.print("TENSION_DROP_MONITOR_ARMED: "); Serial.println(tension.dropMonitorArmed ? 1 : 0);
  Serial.print("TENSION_DROP_BASELINE_G: "); Serial.println(tension.dropBaseline_g);
  Serial.print("TENSION_DROP_THRESHOLD_G: "); Serial.println(tension.dropThreshold_g);
  Serial.print("TENSION_DROP_CANDIDATE_ACTIVE: "); Serial.println(tension.dropCandidateActive ? 1 : 0);
  Serial.print("TENSION_DROP_CONFIRMED_TURNS: "); Serial.println(tension.dropConfirmedTurns);
  Serial.print("TENSION_DROP_CONFIRM_TURNS_REQUIRED: "); Serial.println(TENSION_DROP_CONFIRM_TURNS);
  Serial.print("TENSION_DROP_KEEP_PCT: "); Serial.println(TENSION_DROP_KEEP_PCT);
  Serial.print("TENSION_DROP_BASELINE_MIN_G: "); Serial.println(TENSION_DROP_BASELINE_MIN_G);

  Serial.println();
  Serial.println("=== CALIBRATION ===");
  Serial.print("HARD_VALID: "); Serial.println(cal.hardValid ? 1 : 0);
  Serial.print("SOFT_VALID: "); Serial.println(cal.softValid ? 1 : 0);
  Serial.print("SOFT_TOP_SAVED: "); Serial.println(cal.softTopSaved ? 1 : 0);
  Serial.print("SOFT_BOTTOM_SAVED: "); Serial.println(cal.softBottomSaved ? 1 : 0);
  Serial.print("HARD_TOP: "); Serial.println(cal.hardTopSteps);
  Serial.print("HARD_BOTTOM: "); Serial.println(cal.hardBottomSteps);
  Serial.print("SOFT_TOP: "); Serial.println(cal.softTopSteps);
  Serial.print("SOFT_BOTTOM: "); Serial.println(cal.softBottomSteps);
  Serial.print("ACTIVE_TOP: "); Serial.println(activeTop);
  Serial.print("ACTIVE_BOTTOM: "); Serial.println(activeBottom);

  Serial.println();
  Serial.println("=== DEFCAL REBUILD DATA ===");
  Serial.println("Copy this whole section after a fresh cal + savet + saveb.");
  Serial.print("DEF_HARD_VALID: "); Serial.println(cal.hardValid ? 1 : 0);
  Serial.print("DEF_SOFT_VALID: "); Serial.println(cal.softValid ? 1 : 0);
  Serial.print("DEF_SOFT_TOP_SAVED: "); Serial.println(cal.softTopSaved ? 1 : 0);
  Serial.print("DEF_SOFT_BOTTOM_SAVED: "); Serial.println(cal.softBottomSaved ? 1 : 0);
  Serial.print("DEF_HARD_TOP_STEPS: "); Serial.println(cal.hardTopSteps);
  Serial.print("DEF_HARD_BOTTOM_STEPS: "); Serial.println(cal.hardBottomSteps);
  Serial.print("DEF_SOFT_TOP_STEPS: "); Serial.println(cal.softTopSteps);
  Serial.print("DEF_SOFT_BOTTOM_STEPS: "); Serial.println(cal.softBottomSteps);
  Serial.print("DEF_ACTIVE_TOP_STEPS: "); Serial.println(activeTop);
  Serial.print("DEF_ACTIVE_BOTTOM_STEPS: "); Serial.println(activeBottom);
  Serial.print("DEF_OFFTOP_UM: "); Serial.println(cfg.offTop_um);
  Serial.print("DEF_OFFBOTTOM_UM: "); Serial.println(cfg.offBottom_um);
  Serial.print("DEF_SOFTDIST_UM: "); Serial.println(cfg.softDist_um);
  Serial.print("DEF_SOFTPCT: "); Serial.println(cfg.softPct);
  Serial.print("DEF_CHAOS_ENABLE: "); Serial.println(cfg.chaosEnable ? 1 : 0);
  Serial.print("DEF_CHAOS_PCT: "); Serial.println(cfg.chaosPct);
  Serial.print("DEF_CHAOS_PERSIST_TURNS: "); Serial.println(cfg.chaosPersistTurns);
  Serial.print("DEF_RAMP_RPM_PER_SEC: "); Serial.println(cfg.rampRPMperSec);
  Serial.print("DEF_TRAV_STEP_UM: "); Serial.println(TRAV_STEP_UM);
  Serial.print("DEF_TRAV_LEAD_UM_PER_REV: "); Serial.println(TRAV_LEAD_UM_PER_REV);

  if (cal.hardValid && cal.softValid) {
    int32_t topInsetSteps = cal.hardTopSteps - cal.softTopSteps;
    int32_t bottomInsetSteps = cal.softBottomSteps - cal.hardBottomSteps;
    Serial.print("DEF_TOP_INSET_STEPS: "); Serial.println(topInsetSteps);
    Serial.print("DEF_BOTTOM_INSET_STEPS: "); Serial.println(bottomInsetSteps);
    Serial.print("DEF_TOP_INSET_MM: "); Serial.println(stepsToMm(topInsetSteps), 3);
    Serial.print("DEF_BOTTOM_INSET_MM: "); Serial.println(stepsToMm(bottomInsetSteps), 3);
    Serial.print("DEF_REBUILD_STRING: ");
    Serial.print("CALPROFILE_V2,");
    Serial.print(topInsetSteps); Serial.print(",");
    Serial.print(bottomInsetSteps); Serial.print(",");
    Serial.print(cfg.offTop_um); Serial.print(",");
    Serial.print(cfg.offBottom_um); Serial.print(",");
    Serial.print(cfg.softDist_um); Serial.print(",");
    Serial.print(cfg.softPct); Serial.print(",");
    Serial.print(cfg.chaosEnable ? 1 : 0); Serial.print(",");
    Serial.print(cfg.chaosPct); Serial.print(",");
    Serial.print(cfg.chaosPersistTurns); Serial.print(",");
    Serial.println(cfg.rampRPMperSec);
  } else {
    Serial.println("DEF_TOP_INSET_STEPS: unavailable until hard and soft calibration are both valid");
    Serial.println("DEF_BOTTOM_INSET_STEPS: unavailable until hard and soft calibration are both valid");
    Serial.println("DEF_REBUILD_STRING: unavailable until hard and soft calibration are both valid");
  }

  Serial.println();
  Serial.println("=== CAL PROFILE ===");
  Serial.print("PROFILE_VALID: "); Serial.println(calProfile.valid ? 1 : 0);
  Serial.print("PROFILE_TOP_INSET: "); Serial.println(calProfile.topInsetSteps);
  Serial.print("PROFILE_BOTTOM_INSET: "); Serial.println(calProfile.bottomInsetSteps);
  Serial.print("PROFILE_OFFTOP_UM: "); Serial.println(calProfile.offTop_um);
  Serial.print("PROFILE_OFFBOTTOM_UM: "); Serial.println(calProfile.offBottom_um);
  Serial.print("PROFILE_SOFTDIST_UM: "); Serial.println(calProfile.softDist_um);
  Serial.print("PROFILE_SOFTPCT: "); Serial.println(calProfile.softPct);
  Serial.print("PROFILE_CHAOS_ENABLE: "); Serial.println(calProfile.chaosEnable ? 1 : 0);
  Serial.print("PROFILE_CHAOS_PCT: "); Serial.println(calProfile.chaosPct);
  Serial.print("PROFILE_CHAOS_PERSIST_TURNS: "); Serial.println(DEFAULT_CHAOS_PERSIST_TURNS);
  Serial.print("PROFILE_RAMP_RPM_PER_SEC: "); Serial.println(DEFAULT_RAMP);

  Serial.println();
}

void printHelp() {
  Serial.println();
  Serial.println("==================================================");
  Serial.println("OPEN WINDER COMMANDS");
  Serial.println("==================================================");
  Serial.println();
  Serial.println("[RUN / MOTION]");
  Serial.println("  start");
  Serial.println("  pause");
  Serial.println("  resume");
  Serial.println("  stop");
  Serial.println("  spin <rpm> <cw|ccw>       // free spin, no traverse, ramps to speed");
  Serial.println();
  Serial.println("[REQUIRED JOB SETTINGS]");
  Serial.println("  dir <cw|ccw>");
  Serial.println("  rpm <number>");
  Serial.println("  turns <number>");
  Serial.println("  space <mm>");
  Serial.println();
  Serial.println("[OPTIONAL JOB SETTINGS]");
  Serial.println("  ramp <rpm_per_sec>");
  Serial.println("  offtop <mm>               // adds to the current offset (cumulative)");
  Serial.println("  offbottom <mm>            // adds to the current offset (cumulative)");
  Serial.println("  offreset");
  Serial.println("  softdist <mm>");
  Serial.println("  softpct <0-100>");
  Serial.println("  chaos <0-100> <turn_persistence>");
  Serial.println("  chaosoff");
  Serial.println("  chaoson");
  Serial.println("  mid <offset_mm> <turns>");
  Serial.println("  midclear");
  Serial.println("  driftcomp <steps> <turns> // correct traverse counter by <steps> spread over every <turns> turns;");
  Serial.println("                            //   positive = wind drifts toward TOP; use the RECAL_DRIFT suggestion");
  Serial.println("  driftcompoff");
  Serial.println();
  Serial.println("[CALIBRATION]");
  Serial.println("  cal                       // find hard limits, then jog/save working bounds");
  Serial.println("  defcal                    // find hard limits, then apply default CALPROFILE_V2 window");
  Serial.println("  exportcal                 // print current reconstructed profile/window");
  Serial.println("  jogt / jogb               // continuous jog in CAL_JOG (stop to halt)");
  Serial.println("  nudget <mm> / nudgeb <mm>");
  Serial.println("  savet / saveb");
  Serial.println("  donecal / caldone / finishcal");
  Serial.println();
  Serial.println("[TAPS]");
  Serial.println("  taps <0-3>");
  Serial.println("  tap1 <count>");
  Serial.println("  tap2 <count>");
  Serial.println("  tap3 <count>");
  Serial.println();
  Serial.println("[STATUS / FAULTS]");
  Serial.println("  stat                      // includes DEFCAL REBUILD DATA after cal/saveb");
  Serial.println("  entropy");
  Serial.println("  ack                       // clear fault; if fault interrupted auto wind, state becomes PAUSED_AUTO so resume can continue");
  Serial.println();
  Serial.println("[TENSION / HX711]");
  Serial.println("  tension                   // print tension/HX711 status");
  Serial.println("  zero                      // capture unloaded HX711 zero; clears prior reference calibration");
  Serial.println("  tare <grams>              // capture known mass as negative reference force");
  Serial.println("  ploton                    // Serial Plotter lines on: Min1s_g Max1s_g Avg3s_g after calibration");
  Serial.println("  plotoff                   // Serial Plotter lines off");
  Serial.println("  tensionon                 // enable HX711 reading");
  Serial.println("  tensionoff                // disable HX711 reading");
  Serial.println("  tdrop on|off              // enable/disable adaptive tension-drop fault");
  Serial.println();
  Serial.println("[RESET]");
  Serial.println("  reset                     // full reset but KEEPS the HX711 zero + tare calibration");
  Serial.println("  resetall                  // full reset including tension calibration");
  Serial.println();
  Serial.println("==================================================");
  Serial.println();
}

// -----------------------------------------------------------------------------
// Command processor
// -----------------------------------------------------------------------------
void processCommand(String line) {
  trimLine(line);
  if (line.length() == 0) return;

  String lower = line;
  lower.toLowerCase();

  int pos = 0;
  String cmd = nextToken(lower, pos);

  if (cmd == "help" || cmd == "?") { printHelp(); return; }
  if (cmd == "stat") { printStatus(); return; }
  if (cmd == "entropy") { printEntropyInfo(); return; }

  if (cmd == "tension") { printTensionStatus(); return; }

  if (cmd == "zero" || cmd == "tensionzero") {
    if (machineMoving()) {
      // Blocks the loop for up to ~3 s while averaging; freezing step generation
      // mid-motion can snap wire at speed.
      Serial.println("ERR stop all motion before zero (blocking HX711 read)");
      return;
    }
    tensionZero(TENSION_CAL_SAMPLES);
    return;
  }

  if (cmd == "tare" || cmd == "tensiontare") {
    if (machineMoving()) {
      Serial.println("ERR stop all motion before tare (blocking HX711 read)");
      return;
    }
    String gramsTok = nextToken(lower, pos);
    float grams = 0.0f;
    if (!parseFloatVal(gramsTok, grams) || grams <= 0.0f) {
      Serial.println("ERR use tare <grams>, for example: tare 100");
      return;
    }
    tensionTareKnownMass(grams, TENSION_CAL_SAMPLES);
    return;
  }

  if (cmd == "ploton") {
    tension.plotEnabled = true;
    tensionResetWindow();
    Serial.println("OK tension plot on");
    return;
  }

  if (cmd == "plotoff") {
    tension.plotEnabled = false;
    Serial.println("OK tension plot off");
    return;
  }

  if (cmd == "tensionon") {
    tension.enabled = true;
    tensionResetWindow();
    Serial.println("OK tension on");
    return;
  }

  if (cmd == "tensionoff") {
    tension.enabled = false;
    Serial.println("OK tension off");
    return;
  }

  if (cmd == "tdrop") {
    String mode = nextToken(lower, pos);
    if (mode == "on") {
      tension.dropFaultEnabled = true;
      resetTensionDropFaultMonitor();
      Serial.println("OK tension drop fault on");
      return;
    }
    if (mode == "off") {
      tension.dropFaultEnabled = false;
      resetTensionDropFaultMonitor();
      Serial.println("OK tension drop fault off");
      return;
    }
    Serial.println("ERR use tdrop on or tdrop off");
    return;
  }

  if (cmd == "ack") {
    refreshInputs();

    if (st.state != MachineState::FAULT_FAILSAFE &&
        st.state != MachineState::FAULT_SNAG &&
        st.state != MachineState::FAULT_TENSION) {
      Serial.println("ERR no active fault");
      return;
    }

    if (!permitOK()) {
      Serial.println("ERR permit fault still active");
      printFaultInstructions(MachineState::FAULT_FAILSAFE);
      return;
    }

    if (!snagOK()) {
      Serial.println("ERR snag fault still active");
      printFaultInstructions(MachineState::FAULT_SNAG);
      return;
    }

    hardStopNow();
    resetTensionDropFaultMonitor();

    // A fault that interrupted a calibration invalidated the bounds, and the
    // saved run snapshot would be orphaned. Restart the same calibration so the
    // run can still be restored instead of stranding it in IDLE.
    if (recalSnap.valid && !cal.softValid) {
      if (calMode == CalMode::AUTO_RECAL) {
        enterState(MachineState::RUNNING_AUTO);
        st.currentRPM = 0;
        startCalibration(CalMode::AUTO_RECAL);
        Serial.println("OK fault cleared; interrupted auto recal restarted, run will resume automatically");
        return;
      }
      if (calNeedsRestoreProgress) {
        enterState(calReturnState);
        startCalibration(calMode == CalMode::MANUAL ? CalMode::MANUAL : CalMode::PROFILE_TEMPLATE);
        Serial.println("OK fault cleared; interrupted calibration restarted, paused run will be restored");
        return;
      }
    }

    if (faultResumeAvailable && cal.softValid) {
      if (faultReturnState == MachineState::TAP_PAUSE) enterState(MachineState::TAP_PAUSE);
      else enterState(MachineState::PAUSED_AUTO);
    } else if (cal.softValid) {
      enterState(MachineState::READY);
    } else {
      enterState(MachineState::IDLE);
    }

    Serial.print("OK fault cleared, state=");
    Serial.println(stateName(st.state));
    if (st.state == MachineState::PAUSED_AUTO || st.state == MachineState::TAP_PAUSE) {
      Serial.println("Use: resume to continue from the saved turn count, or stop to terminate");
    } else {
      Serial.println("Use: start or spin <rpm> <cw|ccw>");
    }
    return;
  }

  if (cmd == "start") { startAutoRun(); return; }
  if (cmd == "pause") { pauseAutoRun(); return; }
  if (cmd == "resume") { resumeRun(); return; }
  if (cmd == "stop") { stopAction(); return; }

  if (cmd == "spin") {
    String a = nextToken(lower, pos);
    String b = nextToken(lower, pos);
    uint32_t rpm = 0;
    if (!parseUInt(a, rpm) || (b != "cw" && b != "ccw")) {
      Serial.println("ERR use: spin <rpm> <cw|ccw>");
      return;
    }
    startSpin(rpm, b == "cw");
    return;
  }

  if (cmd == "dir") {
    String a = nextToken(lower, pos);
    if (a != "cw" && a != "ccw") {
      Serial.println("ERR use: dir <cw|ccw>");
      return;
    }
    cfg.winderCW = (a == "cw");
    setupFlags.haveDir = true;
    Serial.print("OK dir=");
    Serial.println(a);
    return;
  }

  if (cmd == "rpm") {
    String a = nextToken(lower, pos);
    uint32_t v = 0;
    if (!parseUInt(a, v) || v < MIN_RPM || v > MAX_RPM) {
      Serial.println("ERR rpm out of range");
      return;
    }
    cfg.rpm = v;
    setupFlags.haveRPM = true;
    Serial.print("OK rpm=");
    Serial.println(cfg.rpm);
    return;
  }

  if (cmd == "turns") {
    String a = nextToken(lower, pos);
    uint32_t v = 0;
    if (!parseUInt(a, v) || v == 0) {
      Serial.println("ERR turns invalid");
      return;
    }
    cfg.totalTurns = v;
    setupFlags.haveTurns = true;
    Serial.print("OK turns=");
    Serial.println(cfg.totalTurns);
    return;
  }

  if (cmd == "space") {
    String a = nextToken(lower, pos);
    float mm = 0.0f;
    if (!parseFloatVal(a, mm) || mm < 0.0f) {
      Serial.println("ERR space invalid");
      return;
    }
    cfg.space_um = mmToUm(mm);
    setupFlags.haveSpace = true;
    Serial.print("OK space=");
    Serial.println(mm, 3);
    return;
  }

  if (cmd == "ramp") {
    String a = nextToken(lower, pos);
    uint32_t v = 0;
    if (!parseUInt(a, v) || v < MIN_RAMP_RPM_S) {
      Serial.println("ERR ramp too low");
      return;
    }
    cfg.rampRPMperSec = v;
    Serial.print("OK ramp=");
    Serial.println(cfg.rampRPMperSec);
    return;
  }

  if (cmd == "offtop") {
    String a = nextToken(lower, pos);
    float mm = 0.0f;
    if (!parseFloatVal(a, mm)) {
      Serial.println("ERR offtop invalid");
      return;
    }
    cfg.offTop_um += mmToUm(mm);
    updateEffectiveBounds();
    Serial.print("OK offtop=");
    Serial.println(umToMm(cfg.offTop_um), 3);
    return;
  }

  if (cmd == "offbottom") {
    String a = nextToken(lower, pos);
    float mm = 0.0f;
    if (!parseFloatVal(a, mm)) {
      Serial.println("ERR offbottom invalid");
      return;
    }
    cfg.offBottom_um += mmToUm(mm);
    updateEffectiveBounds();
    Serial.print("OK offbottom=");
    Serial.println(umToMm(cfg.offBottom_um), 3);
    return;
  }

  if (cmd == "offreset") {
    cfg.offTop_um = 0;
    cfg.offBottom_um = 0;
    updateEffectiveBounds();
    Serial.println("OK offsets reset");
    return;
  }

  if (cmd == "softdist") {
    String a = nextToken(lower, pos);
    float mm = 0.0f;
    if (!parseFloatVal(a, mm) || mm < 0.0f) {
      Serial.println("ERR softdist invalid");
      return;
    }
    cfg.softDist_um = mmToUm(mm);
    Serial.print("OK softdist=");
    Serial.println(mm, 3);
    return;
  }

  if (cmd == "softpct") {
    String a = nextToken(lower, pos);
    uint32_t v = 0;
    if (!parseUInt(a, v) || v > 100) {
      Serial.println("ERR softpct invalid");
      return;
    }
    cfg.softPct = (uint8_t)v;
    Serial.print("OK softpct=");
    Serial.println(cfg.softPct);
    return;
  }

  if (cmd == "chaos") {
    String a = nextToken(lower, pos);
    String b = nextToken(lower, pos);
    uint32_t pct = 0;
    uint32_t persist = 0;
    if (!parseUInt(a, pct) || pct > 100 || !parseUInt(b, persist) || persist == 0 || persist > 65535) {
      Serial.println("ERR use: chaos <0-100> <turn_persistence>");
      return;
    }
    cfg.chaosPct = (uint8_t)pct;
    cfg.chaosPersistTurns = (uint16_t)persist;
    cfg.chaosEnable = (cfg.chaosPct > 0);
    chooseNewChaosSpacingOffset();
    Serial.print("OK chaos pct=");
    Serial.print(cfg.chaosPct);
    Serial.print(" persist=");
    Serial.println(cfg.chaosPersistTurns);
    return;
  }

  if (cmd == "chaosoff") {
    cfg.chaosEnable = false;
    rt.chaosSpaceOffset_um = 0;
    rt.chaosTurnsRemaining = 0;
    Serial.println("OK chaos off");
    return;
  }

  if (cmd == "chaoson") {
    cfg.chaosEnable = true;
    if (cfg.chaosPct == 0) cfg.chaosPct = DEFAULT_CHAOS_PCT;
    if (cfg.chaosPersistTurns == 0) cfg.chaosPersistTurns = DEFAULT_CHAOS_PERSIST_TURNS;
    chooseNewChaosSpacingOffset();
    Serial.print("OK chaos on pct=");
    Serial.print(cfg.chaosPct);
    Serial.print(" persist=");
    Serial.println(cfg.chaosPersistTurns);
    return;
  }

  if (cmd == "mid") {
    String a = nextToken(lower, pos);
    String b = nextToken(lower, pos);

    float mm = 0.0f;
    uint32_t turns = 0;
    if (!parseFloatVal(a, mm) || !parseUInt(b, turns) || turns == 0) {
      Serial.println("ERR use: mid <offset_mm> <turns>");
      return;
    }

    clearActiveMid();
    clearArmedMid();

    if (st.state == MachineState::RUNNING_AUTO ||
        st.state == MachineState::PAUSED_AUTO ||
        st.state == MachineState::TAP_PAUSE) {
      rt.midActive = true;
      rt.midInset_um = mmToUm(mm);
      rt.midTurnsRemaining = turns;
      updateEffectiveBounds();
      Serial.print("OK mid immediate inset=");
      Serial.print(mm, 3);
      Serial.print(" turns=");
      Serial.println(turns);
    } else {
      rt.midArmed = true;
      rt.midArmedInset_um = mmToUm(mm);
      rt.midArmedTurns = turns;
      Serial.print("OK mid armed inset=");
      Serial.print(mm, 3);
      Serial.print(" turns=");
      Serial.println(turns);
    }
    return;
  }

  if (cmd == "midclear") {
    clearActiveMid();
    clearArmedMid();
    updateEffectiveBounds();
    Serial.println("OK mid cleared");
    return;
  }

  if (cmd == "driftcomp") {
    String a = nextToken(lower, pos);
    String b = nextToken(lower, pos);
    int32_t steps = 0;
    uint32_t turns = 0;
    if (!parseInt32Val(a, steps) || !parseUInt(b, turns)) {
      Serial.println("ERR use: driftcomp <steps> <turns>   (positive = wind drifting toward TOP; driftcomp 0 0 = off)");
      return;
    }
    if (turns == 0) steps = 0;
    cfg.driftCompSteps = steps;
    cfg.driftCompTurns = turns;
    rt.driftCompAccum = 0;
    if (turns == 0) {
      Serial.println("OK driftcomp off");
    } else {
      Serial.print("OK driftcomp steps=");
      Serial.print(cfg.driftCompSteps);
      Serial.print(" (");
      Serial.print(stepsToMm(cfg.driftCompSteps), 3);
      Serial.print(" mm) over every ");
      Serial.print(cfg.driftCompTurns);
      Serial.println(" turns");
    }
    return;
  }

  if (cmd == "driftcompoff") {
    cfg.driftCompSteps = 0;
    cfg.driftCompTurns = 0;
    rt.driftCompAccum = 0;
    Serial.println("OK driftcomp off");
    return;
  }

  if (cmd == "taps") {
    String a = nextToken(lower, pos);
    uint32_t v = 0;
    if (!parseUInt(a, v) || v > 3) {
      Serial.println("ERR taps invalid");
      return;
    }
    cfg.tapCount = (uint8_t)v;
    for (uint8_t i = cfg.tapCount; i < 3; ++i) cfg.tapTargets[i] = 0;
    normalizeTapConfig(false);
    Serial.print("OK taps=");
    Serial.println(cfg.tapCount);
    return;
  }

  if (cmd == "tap1" || cmd == "tap2" || cmd == "tap3") {
    uint8_t idx = (uint8_t)(cmd[3] - '1');
    String a = nextToken(lower, pos);
    uint32_t v = 0;
    if (!parseUInt(a, v)) {
      Serial.println("ERR tap target invalid");
      return;
    }
    cfg.tapTargets[idx] = v;
    if (cfg.tapCount < idx + 1) cfg.tapCount = idx + 1;
    normalizeTapConfig(true);
    Serial.print("OK ");
    Serial.print(cmd);
    Serial.print("=");
    Serial.println(v);
    return;
  }

  if (cmd == "cal") {
    startCalibration(CalMode::MANUAL);
    return;
  }

  if (cmd == "defcal") {
    startDefaultDefcal(true);
    return;
  }

  if (cmd == "jogt") {
    if (st.state != MachineState::CAL_JOG) {
      Serial.println("ERR not in CAL_JOG");
      return;
    }
    if (topLimitTriggered()) {
      Serial.println("ERR already at TOP limit");
      return;
    }
    jogDir = 1;
    setTraverseRPM(JOG_RPM, TraverseDir::TO_TOP);
    Serial.println("OK jog top");
    return;
  }

  if (cmd == "jogb") {
    if (st.state != MachineState::CAL_JOG) {
      Serial.println("ERR not in CAL_JOG");
      return;
    }
    if (bottomLimitTriggered()) {
      Serial.println("ERR already at BOTTOM limit");
      return;
    }
    jogDir = -1;
    setTraverseRPM(JOG_RPM, TraverseDir::TO_BOTTOM);
    Serial.println("OK jog bottom");
    return;
  }

  if (cmd == "nudget") {
    if (st.state != MachineState::CAL_JOG) {
      Serial.println("ERR not in CAL_JOG");
      return;
    }
    String a = nextToken(lower, pos);
    float mm = 0.0f;
    if (!parseFloatVal(a, mm) || mm <= 0.0f) {
      Serial.println("ERR nudget invalid");
      return;
    }
    nudgeTraverseMm(mm, TraverseDir::TO_TOP);
    Serial.print("OK nudget ");
    Serial.println(mm, 3);
    return;
  }

  if (cmd == "nudgeb") {
    if (st.state != MachineState::CAL_JOG) {
      Serial.println("ERR not in CAL_JOG");
      return;
    }
    String a = nextToken(lower, pos);
    float mm = 0.0f;
    if (!parseFloatVal(a, mm) || mm <= 0.0f) {
      Serial.println("ERR nudgeb invalid");
      return;
    }
    nudgeTraverseMm(mm, TraverseDir::TO_BOTTOM);
    Serial.print("OK nudgeb ");
    Serial.println(mm, 3);
    return;
  }

  if (cmd == "savet") {
    if (st.state != MachineState::CAL_JOG || !cal.hardValid) {
      Serial.println("ERR not ready to savet");
      return;
    }

    int32_t previousTop = cal.softTopSteps;
    bool previousTopSaved = cal.softTopSaved;

    cal.softTopSteps = traverse.positionSteps;
    cal.softTopSaved = true;

    if (cal.softBottomSaved && cal.softTopSteps <= cal.softBottomSteps) {
      Serial.println("ERR top must be above bottom");
      cal.softTopSteps = previousTop;
      cal.softTopSaved = previousTopSaved;
      return;
    }

    if (cal.softBottomSaved) {
      cal.softValid = true;
      updateEffectiveBounds();
      updateStoredCalProfileFromCurrentCalibration();
      Serial.print("OK top saved=");
      Serial.println(cal.softTopSteps);
      Serial.println("OK calibration valid; still in CAL_JOG so you can adjust/save either side again");
      Serial.println("Send donecal when finished.");
    } else {
      cal.softValid = false;
      Serial.print("OK top saved=");
      Serial.println(cal.softTopSteps);
      Serial.println("OK now set/save bottom bound with jogb/nudgeb then saveb");
    }
    return;
  }

  if (cmd == "saveb") {
    if (st.state != MachineState::CAL_JOG || !cal.hardValid) {
      Serial.println("ERR not ready to saveb");
      return;
    }

    int32_t previousBottom = cal.softBottomSteps;
    bool previousBottomSaved = cal.softBottomSaved;

    cal.softBottomSteps = traverse.positionSteps;
    cal.softBottomSaved = true;

    if (cal.softTopSaved && cal.softTopSteps <= cal.softBottomSteps) {
      Serial.println("ERR bottom must be below top");
      cal.softBottomSteps = previousBottom;
      cal.softBottomSaved = previousBottomSaved;
      return;
    }

    if (cal.softTopSaved) {
      cal.softValid = true;
      updateEffectiveBounds();
      updateStoredCalProfileFromCurrentCalibration();
      Serial.print("OK bottom saved=");
      Serial.println(cal.softBottomSteps);
      Serial.println("OK calibration valid; still in CAL_JOG so you can adjust/save either side again");
      Serial.println("Send donecal when finished.");
    } else {
      cal.softValid = false;
      Serial.print("OK bottom saved=");
      Serial.println(cal.softBottomSteps);
      Serial.println("OK now set/save top bound with jogt/nudget then savet");
    }
    return;
  }

  if (cmd == "donecal" || cmd == "caldone" || cmd == "finishcal") {
    if (st.state != MachineState::CAL_JOG) {
      Serial.println("ERR not in CAL_JOG");
      return;
    }
    if (!cal.hardValid || !cal.softTopSaved || !cal.softBottomSaved || !cal.softValid) {
      Serial.println("ERR cannot finish calibration until both top and bottom are saved");
      return;
    }
    stopTraverse();
    jogDir = 0;
    updateEffectiveBounds();
    updateStoredCalProfileFromCurrentCalibration();

    if (calNeedsRestoreProgress && recalSnap.valid) {
      // This cal was started from a paused run: put the carriage back where it
      // was (relative to the new window) and return to the pause, run intact.
      printRecalDrift();
      setupRestoreTargetFromCurrentEffectiveBounds();
      calFindSub = CalFindSubstate::RESTORE_PROGRESS;
      enterState(MachineState::CAL_FIND_LIMITS);
      Serial.println("OK calibration finished; restoring traverse position, then back to the paused run");
      Serial.println("Run stat and copy the DEFCAL REBUILD DATA section.");
      return;
    }

    enterState(MachineState::READY);
    Serial.println("OK calibration finished, state=READY");
    Serial.println("Run stat and copy the DEFCAL REBUILD DATA section.");
    return;
  }

  if (cmd == "exportcal") {
    exportCalProfile();
    return;
  }

  if (cmd == "importcal") {
    Serial.println("ERR importcal disabled; use defcal or run cal/jog/saveb");
    return;
  }

  if (cmd == "applycalprofile") {
    Serial.println("ERR applycalprofile disabled; use defcal or run cal/jog/saveb");
    return;
  }

  if (cmd == "clearcalprofile") {
    clearCalProfile();
    return;
  }

  if (cmd == "reset") {
    resetKeepTension();
    return;
  }

  if (cmd == "resetall") {
    resetAll();
    return;
  }

  Serial.println("ERR unknown command");
}

// -----------------------------------------------------------------------------
// Loop helpers
// -----------------------------------------------------------------------------
void handleStartMoveToBottom() {
  if (startPhase != StartPhase::MOVE_TO_EFFECTIVE_BOTTOM || !rt.autoStartPending) return;

  if (bottomLimitTriggered() || atEffectiveBottom()) {
    stopTraverse();
    rt.autoStartPending = false;
    startPhase = StartPhase::NONE;
    prepareTensionForRunStart();
    winder.forceDir(cfg.winderCW ^ WINDER_DIR_INVERT);
    beginWinderWithVisibleFloor(cfg.rpm);
    enterState(MachineState::RUNNING_AUTO);
    statusTimer = 0;
    Serial.println("OK auto run started");
    return;
  }

  setTraverseRPM(CAL_CENTER_RPM, TraverseDir::TO_BOTTOM);
}

void handleStoppedAutoTransitions() {
  if (st.state != MachineState::RUNNING_AUTO &&
      st.state != MachineState::PAUSED_AUTO &&
      st.state != MachineState::TAP_PAUSE) {
    return;
  }

  if (st.currentRPM != 0) return;
  if (st.state != MachineState::RUNNING_AUTO) return;

  // Priority: stop > pause > tap > auto-recal.
  // Stop outranks pause so a stop sent during a pause decel actually stops.
  // Tap outranks recal so a tap that landed during a recal decel still pauses;
  // resume re-arms the recal (see resumeRun).
  if (rt.stopRequested) {
    rt.stopRequested = false;
    rt.pauseRequested = false;
    hardStopNow();
    enterState(MachineState::TERMINATED);
    Serial.println("OK auto stopped");
    return;
  }

  if (rt.pauseRequested) {
    rt.pauseRequested = false;
    hardStopNow();
    enterState(MachineState::PAUSED_AUTO);
    Serial.println("OK paused");
    return;
  }

  if (rt.activeTapIndex >= 0) {
    hardStopNow();
    enterState(MachineState::TAP_PAUSE);
    Serial.print("OK tap pause at tap");
    Serial.println(rt.activeTapIndex + 1);
    return;
  }

  if (rt.autoRecalPending) {
    saveRecalSnapshot();
    startCalibration(CalMode::AUTO_RECAL);
    return;
  }
}

// -----------------------------------------------------------------------------
// Turn count handler (one commanded winder step per call)
// -----------------------------------------------------------------------------
void handleWinderStepEvents() {
  if (st.state != MachineState::RUNNING_AUTO) return;

  rt.stepInTurn++;
  handleTraverseDemandOnWinderStep();

  if (rt.stepInTurn >= WINDER_STEPS_PER_REV) {
    rt.stepInTurn = 0;
    rt.turnsDone++;

    applyDriftCompOnTurn();

    updateTensionDropFaultMonitorOnTurn();
    if (st.state == MachineState::FAULT_TENSION) return;

    if (cfg.chaosEnable && cfg.chaosPct > 0) {
      if (rt.chaosTurnsRemaining > 0) {
        rt.chaosTurnsRemaining--;
      }
      if (rt.chaosTurnsRemaining == 0) {
        chooseNewChaosSpacingOffset();
      }
    } else {
      rt.chaosSpaceOffset_um = 0;
      rt.chaosTurnsRemaining = 0;
    }

    if (rt.midActive && rt.midTurnsRemaining > 0) {
      rt.midTurnsRemaining--;
      if (rt.midTurnsRemaining == 0) {
        clearActiveMid();
      }
    }

    if (checkTapTrigger()) {
      return;
    }

    uint32_t antic = decelAnticipationTurns();

    if (!rt.autoRecalPending &&
        rt.turnsDone + antic >= rt.nextAutoRecalTurn &&
        st.state == MachineState::RUNNING_AUTO) {
      rt.autoRecalPending = true;
      commandedWinderRPM = 0.0f;
      return;
    }

    if (!rt.stopRequested && rt.turnsDone + antic >= cfg.totalTurns) {
      commandedWinderRPM = 0.0f;
      rt.stopRequested = true;
    }
  }
}

// -----------------------------------------------------------------------------
// Arduino
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(50);
  Entropy.Initialize();

#if USE_WATCHDOG
  {
    WDT_timings_t wdtConfig;
    wdtConfig.timeout = WDT_TIMEOUT_S;
    wdt.begin(wdtConfig);
  }
#endif

  pinMode(PIN_BOTTOM_LIMIT, INPUT_PULLUP);
  pinMode(PIN_TOP_LIMIT, INPUT_PULLUP);
#if HAS_RUN_PERMIT
  pinMode(PIN_RUN_PERMIT, INPUT_PULLUP);
#endif
#if HAS_SNAG_FAULT
  pinMode(PIN_SNAG_FAULT, INPUT_PULLUP);
#endif

  topLimitInput.begin(PIN_TOP_LIMIT, true);
  bottomLimitInput.begin(PIN_BOTTOM_LIMIT, true);

  winder.begin(PIN_WINDER_STEP, PIN_WINDER_DIR, false ^ WINDER_DIR_INVERT, WINDER_DIR_INVERT);
  traverse.begin(PIN_TRAV_STEP, PIN_TRAV_DIR, false ^ TRAVERSE_DIR_INVERT, TRAVERSE_DIR_INVERT);

  hx711Begin();
  tensionResetWindow();
  resetTensionDropFaultMonitor();

  refreshInputs();
  seedChaosRng();
  enterState(MachineState::IDLE);

  Serial.println();
  Serial.println("Open Winder Ready - public release (from rev 2026-09-16b)");
  Serial.println("  winder 400 steps/rev, traverse 400 steps/rev (BOTH DM542: SW5 OFF, SW6 ON, SW7 ON, SW8 ON)");
  Serial.println("  HX711 on pins 30/31 at 3.3 V, limits 7=BOTTOM 8=TOP, permit/snag not fitted");
  printHelp();
}

void loop() {
  wdtFeed();
  refreshInputs();
  handleFaults();
  updateTension();

  uint32_t nowUs = micros();

  if (st.state == MachineState::RUNNING_AUTO ||
      st.state == MachineState::RUNNING_SPIN) {
    updateWinderRamp();
  }

  bool winderStepped = winder.update(nowUs);
  (void)traverse.update(nowUs);

  if (winderStepped && st.state == MachineState::RUNNING_AUTO) {
    handleWinderStepEvents();
  }

  if (st.state == MachineState::CAL_FIND_LIMITS) {
    updateCalibrationFind();
  }

  // CAL_JOG runs the traverse continuously with nothing else watching the
  // limits; stop it before it drives the carriage into the hard stop.
  if (st.state == MachineState::CAL_JOG && jogDir != 0) {
    if ((jogDir > 0 && topLimitTriggered()) || (jogDir < 0 && bottomLimitTriggered())) {
      stopTraverse();
      jogDir = 0;
      Serial.println("OK jog stopped at limit switch");
    }
  }

  if (st.state == MachineState::READY && rt.autoStartPending) {
    handleStartMoveToBottom();
  }

  handleStoppedAutoTransitions();

  if (st.state == MachineState::RUNNING_AUTO && statusTimer >= STATUS_INTERVAL_MS) {
    printRunStatusLine();
    statusTimer = 0;
  }

  serviceUsbCommandPort(Serial, rxLine);
}
