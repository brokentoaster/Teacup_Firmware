#include	"pinio.h"

void power_off() {

	x_disable();
	y_disable();
	z_disable();

	#ifdef	STEPPER_ENABLE_PIN
	WRITE(STEPPER_ENABLE_PIN, STEPPER_ENABLE_INVERT ^ 1);
	#endif
	#ifdef	PS_ON_PIN
	//	Removed this as it turns off the extruder so I can't monitor the temperature
	//	SET_INPUT(PS_ON_PIN);
	#endif
}
