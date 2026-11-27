// unitanim - the offline driver of the unit animation controller (epic 0374, issue 4404 part B; research
// unit-controller.md section 14). Plays a .c5anim timeline through the controller on the install, the mods and the
// database, and writes per frame the programs, triggers and unit statuses (CSV) and the single-source frames as
// .c5units. Usage, outputs and exit codes: c5r/unitanim/driver.h (driverMain).
#include "c5r/unitanim/driver.h"

int main(int argc, char** argv) { return c5r::unitanim::driverMain(argc, argv); }
