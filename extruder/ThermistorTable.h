// default thermistor lookup table

// How many thermistor tables we have
#define NUMTABLES 1

#define THERMISTOR_EXTRUDER	0
// #define THERMISTOR_BED		1

// Thermistor lookup table, generated with --num-temps=50 and trimmed in lower temperature ranges.
// You may be able to improve the accuracy of this table in various ways.
//   1. Measure the actual resistance of the resistor. It's "nominally" 4.7K, but that's ± 5%.
//   2. Measure the actual beta of your thermistor:http://reprap.org/wiki/MeasuringThermistorBeta
//   3. Generate more table entries than you need, then trim down the ones in uninteresting ranges. (done)
// In either case you'll have to regenerate this table, which requires python, which is difficult to install on windows.
// Since you'll have to do some testing to determine the correct temperature for your application anyway, you
// may decide that the effort isn't worth it. Who cares if it's reporting the "right" temperature as long as it's
// keeping the temperature steady enough to print, right?
// ./createTemperatureLookup.py --r0=100000 --t0=25 --r1=0 --r2=4700 --beta=4066 --max-adc=1023
// r0: 100000
// t0: 25
// r1: 0
// r2: 4700
// beta: 4066
// max adc: 1023
#define NUMTEMPS 20
// {ADC, temp*4 }, // temp
uint16_t temptable[NUMTABLES][NUMTEMPS][2] PROGMEM = {
{
	{5, 500*4},
	{6, 474*4},
	{8, 448*4},
	{9, 422*4},
	{12, 396*4},
	{15, 370*4},
	{20, 344*4},
	{26, 318*4},
	{35, 292*4},
	{49, 246*4},   	// hand tuned with thermocouple
	{70, 229*4},	// hand tuned with thermocouple
	{103, 200*4},	// hand tuned with thermocouple
	{155, 180.75*4},	// hand tuned with thermocouple
	{236, 158*4},	// hand tuned with thermocouple
	{359, 136*4},
	{526, 110*4},
	{711, 84*4},
	{867, 58*4},
	{962, 32*4},
	{1005, 6*4}
}
};
