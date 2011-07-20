 #include	"temp.h"

/** \file
	\brief Manage temperature sensors

	\note \b ALL temperatures are stored as 14.2 fixed point in teacup, so we have a range of 0 - 16383.75 celsius and a precision of 0.25 celsius. That includes the ThermistorTable, which is why you can't copy and paste one from other firmwares which don't do this.
*/

#include	<stdlib.h>
#include	<avr/eeprom.h>
#include	<avr/pgmspace.h>

#include	"arduino.h"
#include	"delay.h"
#include	"debug.h"
#ifndef	EXTRUDER
	#include	"sersendf.h"
#endif
#include	"heater.h"
#ifdef	TEMP_INTERCOM
	#include	"intercom.h"
#endif

#ifdef	TEMP_MAX6675
#endif

#ifdef	TEMP_THERMISTOR
#include	"analog.h"
#include	"ThermistorTable.h"
#endif

#ifdef	TEMP_AD595
#include	"analog.h"
#endif

typedef enum {
	PRESENT,
	TCOPEN
} temp_flags_enum;

/// holds metadata for each temperature sensor
typedef struct {
	temp_type_t temp_type; ///< type of sensor
	uint8_t     temp_pin;  ///< pin that sensor is on
	heater_t    heater;    ///< associated heater if any
	uint8_t		additional; ///< additional, sensor type specifc config
} temp_sensor_definition_t;

#undef DEFINE_TEMP_SENSOR
/// help build list of sensors from entries in config.h
#define DEFINE_TEMP_SENSOR(name, type, pin, additional) { (type), (pin), (HEATER_ ## name), (additional) },
static const temp_sensor_definition_t temp_sensors[NUM_TEMP_SENSORS] =
{
	#include	"config.h"
};
#undef DEFINE_TEMP_SENSOR

/// this struct holds the runtime sensor data- read temperatures, targets, etc
struct {
	temp_flags_enum		temp_flags;     ///< flags

	uint16_t					last_read_temp; ///< last received reading
	uint16_t					target_temp;		///< manipulate attached heater to attempt to achieve this value

	uint16_t					temp_residency; ///< how long have we been close to target temperature in temp ticks?

	uint16_t					next_read_time; ///< how long until we can read this sensor again?
} temp_sensors_runtime[NUM_TEMP_SENSORS];

/// set up temp sensors. Currently only the 'intercom' sensor needs initialisation.
void temp_init() {
	temp_sensor_t i;
	for (i = 0; i < NUM_TEMP_SENSORS; i++) {
		switch(temp_sensors[i].temp_type) {
		#ifdef	TEMP_MAX6675
			// initialised when read
/*			case TT_MAX6675:
				break;*/
                #ifdef NAL_REPRAP
                            SET_OUTPUT(MAX6675_CS);
                            SET_OUTPUT(MAX6675_SCK);
                            SET_INPUT(MAX6675_SO);
                #endif
		#endif

		#ifdef	TEMP_THERMISTOR
			// handled by analog_init()
/*			case TT_THERMISTOR:
				break;*/
		#endif

		#ifdef	TEMP_AD595
			// handled by analog_init()
/*			case TT_AD595:
				break;*/
		#endif

		#ifdef	TEMP_INTERCOM
			case TT_INTERCOM:
				intercom_init();
				send_temperature(0, 0);
				break;
		#endif

			default: /* prevent compiler warning */
				break;
		}
	}
}

/// called every 10ms from clock.c - check all temp sensors that are ready for checking
void temp_sensor_tick() {
	temp_sensor_t i = 0;
	for (; i < NUM_TEMP_SENSORS; i++) {
		if (temp_sensors_runtime[i].next_read_time) {
			temp_sensors_runtime[i].next_read_time--;
		}
		else {
			uint16_t	temp = 0;
			//time to deal with this temp sensor
			switch(temp_sensors[i].temp_type) {
				#ifdef	TEMP_MAX6675
				case TT_MAX6675:
					#ifdef	PRR
						PRR &= ~MASK(PRSPI);
					#elif defined PRR0
						PRR0 &= ~MASK(PRSPI);
					#endif
                    #ifdef NAL_REPRAP
                        /* This section is compatible with the mendel original implementation of the MAX6675
                         * not using the SPI as the MISO line is used to control our heater.
                         */

                        WRITE(MAX6675_CS, 0); // Enable device

                        // Read in 16 bits from the MAX6675 
                        for (uint8_t i=0; i<16; i++){
                            WRITE(MAX6675_SCK,1);                               // Set Clock to HIGH
                            temp <<= 1;                                         // shift left by one
                            // Read bit  and add it to our variable
                            temp +=  (READ(MAX6675_SO) != 0 );
                            WRITE(MAX6675_SCK,0);                               // Set Clock to LOW
                        }

                        WRITE(MAX6675_CS, 1);                                   //Disable Device
                    #else
                        SPCR = MASK(MSTR) | MASK(SPE) | MASK(SPR0);

                        // enable TT_MAX6675
                        WRITE(SS, 0);

                        // ensure 100ns delay - a bit extra is fine
                        delay(1);

                        // read MSB
                        SPDR = 0;
                        for (;(SPSR & MASK(SPIF)) == 0;);
                        temp = SPDR;
                        temp <<= 8;

                        // read LSB
                        SPDR = 0;
                        for (;(SPSR & MASK(SPIF)) == 0;);
                        temp |= SPDR;

                        // disable TT_MAX6675
                        WRITE(SS, 1);
                    #ENDIF
					temp_sensors_runtime[i].temp_flags = 0;
					if ((temp & 0x8002) == 0) {
						// got "device id"
						temp_sensors_runtime[i].temp_flags |= PRESENT;
						if (temp & 4) {
							// thermocouple open
							temp_sensors_runtime[i].temp_flags |= TCOPEN;
						}
						else {
							temp = temp >> 3;
						}
					}

					// this number depends on how frequently temp_sensor_tick is called. the MAX6675 can give a reading every 0.22s, so set this to about 250ms
					temp_sensors_runtime[i].next_read_time = 25;

					break;
				#endif	/* TEMP_MAX6675	*/

				#ifdef	TEMP_THERMISTOR
				case TT_THERMISTOR:
					do {
						uint8_t j, table_num;
						//Read current temperature
						temp = analog_read(temp_sensors[i].temp_pin);
						// for thermistors the thermistor table number is in the additional field
						table_num = temp_sensors[i].additional;

						//Calculate real temperature based on lookup table
						for (j = 1; j < NUMTEMPS; j++) {
							if (pgm_read_word(&(temptable[table_num][j][0])) > temp) {
								// Thermistor table is already in 14.2 fixed point
								#ifndef	EXTRUDER
								if (debug_flags & DEBUG_PID)
									sersendf_P(PSTR("pin:%d Raw ADC:%d table entry: %d"),temp_sensors[i].temp_pin,temp,j);
								#endif
								// Linear interpolating temperature value
								// y = ((x - x₀)y₁ + (x₁-x)y₀ ) / (x₁ - x₀)
								// y = temp
								// x = ADC reading
								// x₀= temptable[j-1][0]
								// x₁= temptable[j][0]
								// y₀= temptable[j-1][1]
								// y₁= temptable[j][1]
								// y =
								// Wikipedia's example linear interpolation formula.
								temp = (
								//     ((x - x₀)y₁
									((uint32_t)temp - pgm_read_word(&(temptable[table_num][j-1][0]))) * pgm_read_word(&(temptable[table_num][j][1]))
								//                 +
									+
								//                   (x₁-x)
									(pgm_read_word(&(temptable[table_num][j][0])) - (uint32_t)temp)
								//                         y₀ )
									* pgm_read_word(&(temptable[table_num][j-1][1])))
								//                              /
									/
								//                                (x₁ - x₀)
									(pgm_read_word(&(temptable[table_num][j][0])) - pgm_read_word(&(temptable[table_num][j-1][0])));
								#ifndef	EXTRUDER
								if (debug_flags & DEBUG_PID)
									sersendf_P(PSTR(" temp:%d.%d"),temp/4,(temp%4)*25);
								#endif
								break;
							}
						}
						#ifndef	EXTRUDER
						if (debug_flags & DEBUG_PID)
							sersendf_P(PSTR(" Sensor:%d\n"),i);
						#endif


						//Clamp for overflows
						if (j == NUMTEMPS)
							temp = temptable[table_num][NUMTEMPS-1][1];

						temp_sensors_runtime[i].next_read_time = 0;
					} while (0);
					break;
				#endif	/* TEMP_THERMISTOR */

				#ifdef	TEMP_AD595
				case TT_AD595:
					temp = analog_read(temp_sensors[i].temp_pin);

					// convert
					// >>8 instead of >>10 because internal temp is stored as 14.2 fixed point
					temp = (temp * 500L) >> 8;

					temp_sensors_runtime[i].next_read_time = 0;

					break;
				#endif	/* TEMP_AD595 */

				#ifdef	TEMP_PT100
				case TT_PT100:
					#warning TODO: PT100 code
					break
				#endif	/* TEMP_PT100 */

				#ifdef	TEMP_INTERCOM
				case TT_INTERCOM:
					temp = read_temperature(temp_sensors[i].temp_pin);

					temp_sensors_runtime[i].next_read_time = 25;

					break;
				#endif	/* TEMP_INTERCOM */

				#ifdef	TEMP_DUMMY
				case TT_DUMMY:
					temp = temp_sensors_runtime[i].last_read_temp;

					if (temp_sensors_runtime[i].target_temp > temp)
						temp++;
					else if (temp_sensors_runtime[i].target_temp < temp)
						temp--;

					temp_sensors_runtime[i].next_read_time = 0;

					break;
				#endif	/* TEMP_DUMMY */

				default: /* prevent compiler warning */
					break;
			}
			temp_sensors_runtime[i].last_read_temp = temp;

			if (labs((int16_t)(temp - temp_sensors_runtime[i].target_temp)) < (TEMP_HYSTERESIS*4)) {
				if (temp_sensors_runtime[i].temp_residency < (TEMP_RESIDENCY_TIME*100))
					temp_sensors_runtime[i].temp_residency++;
			}
			else {
				temp_sensors_runtime[i].temp_residency = 0;
			}

			if (temp_sensors[i].heater < NUM_HEATERS) {
				heater_tick(temp_sensors[i].heater, i, temp_sensors_runtime[i].last_read_temp, temp_sensors_runtime[i].target_temp);
			}
		}
	}
}

/// report whether all temp sensors are reading their target temperatures
/// used for M109 and friends
uint8_t	temp_achieved() {
	temp_sensor_t i;
	uint8_t all_ok = 255;

	for (i = 0; i < NUM_TEMP_SENSORS; i++) {
		if (temp_sensors_runtime[i].temp_residency < (TEMP_RESIDENCY_TIME*100))
			all_ok = 0;
	}
	return all_ok;
}

/// specify a target temperature
/// \param index sensor to set a target for
/// \param temperature target temperature to aim for
void temp_set(temp_sensor_t index, uint16_t temperature) {
	if (index >= NUM_TEMP_SENSORS)
		return;

	// only reset residency if temp really changed
	if (temp_sensors_runtime[index].target_temp != temperature) {
		temp_sensors_runtime[index].target_temp = temperature;
		temp_sensors_runtime[index].temp_residency = 0;
	#ifdef	TEMP_INTERCOM
		if (temp_sensors[index].temp_type == TT_INTERCOM)
			send_temperature(temp_sensors[index].temp_pin, temperature);
	#endif
	}
}

/// return most recent reading for a sensor
/// \param index sensor to read
uint16_t temp_get(temp_sensor_t index) {
	if (index >= NUM_TEMP_SENSORS)
		return 0;

	return temp_sensors_runtime[index].last_read_temp;
}

// extruder doesn't have sersendf_P
#ifndef	EXTRUDER
/// send temperatures to host
/// \param index sensor value to send
void temp_print(temp_sensor_t index) {
	uint8_t c = 0;

	if (index >= NUM_TEMP_SENSORS)
		return;

	c = (temp_sensors_runtime[index].last_read_temp & 3) * 25;

	sersendf_P(PSTR("T:%u.%u"), temp_sensors_runtime[index].last_read_temp >> 2, c);
	#ifdef HEATER_BED
		uint8_t b = 0;
		b = (temp_sensors_runtime[HEATER_BED].last_read_temp & 3) * 25;

		sersendf_P(PSTR(" B:%u.%u"), temp_sensors_runtime[HEATER_BED].last_read_temp >> 2 , b);
	#endif

}
#endif
