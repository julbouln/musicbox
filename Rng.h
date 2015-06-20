#ifndef _RNG_H_
#define _RNG_H_

class Rng {

	RNG_HandleTypeDef RNGHandle;

public:
	Rng() {
		__RNG_CLK_ENABLE();
		RNGHandle.Instance = RNG;
		HAL_RNG_Init(&RNGHandle);
	}

	uint32_t randint(uint32_t min, uint32_t max) {
		uint32_t rand = 0;
		if (min == max) {
			return 0;
		}

		// Wait until the RNG is ready
		while (HAL_RNG_GetState(&RNGHandle) != RNG_FLAG_DRDY);

		rand = HAL_RNG_GetRandomNumber(&RNGHandle);
		return (rand % (max - min)) + min;
	}
};

#endif // _RNG_H