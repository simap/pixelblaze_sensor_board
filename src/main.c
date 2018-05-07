
#include "main.h"


enum {
	IDLE, STARTING, SENDING_REG, READING, STOPPING, NEEDSRESET
} I2CMode = NEEDSRESET;


uint32_t audioAverage = 16384<<16;
volatile uint16_t adcBuffer[7]; //updated by DMA @ 20KHz
volatile int16_t accelerometer[3]; //updated by DMA
volatile uint32_t ms = 0; //updated by SysTick


//ping-pong buffer for main 20KHz audio
volatile bool readDone;
int readSide;
int readPos;
int16_t buffer[2][HIGH_N];

//circular buffer for low frequency stuff - we need to reuse parts of it and can afford the memory
//the 32 samples cover 1600 samples of the original audio
struct {
	int16_t circular[32];
	int16_t output[32];
	int head;
	//keep track of how many samples have passed through the filter to know when to sample from it
	int downSampleCounter;
	int32_t avg;
} bufferLowHz;

int main(void) {
	SysTick_Config(SystemCoreClock/1000); //tick interval 1ms
	initRcc();
	initTim1();
	initDma();
	initAdc();
	initGpio();
	initUart();
	initI2C();

	for (;;) {

		if (I2CMode == NEEDSRESET)
			initAccelerometer();

		//listen for message that the sample buffer is ready to process
		if (readDone) {
			readDone = false;

//			GPIO_WriteBit(GPIOB, GPIO_Pin_1, 1);

			//start polling the accelerometer now, it can run in the background while the FFT processes
			startAccelerometerPoll();

			//grab the side we're not currently writing to and do an FFT on it
			processSensorData(&buffer[!readSide][0], &bufferLowHz.output[0], adcBuffer, accelerometer);

//			GPIO_WriteBit(GPIOB, GPIO_Pin_1, 0);
		}

	}
}

void initRcc() {
	//enable adc, dma, uart, i2c, and gpio clocks
	RCC->AHBENR |= RCC_AHBENR_GPIOAEN | RCC_AHBENR_GPIOBEN | RCC_AHBENR_DMAEN;
	RCC->APB1ENR |= RCC_APB1ENR_I2C1EN;
	RCC->APB2ENR |= RCC_APB2ENR_ADC1EN | RCC_APB2ENR_TIM1EN | RCC_APB2ENR_USART1EN;

	//Start HSI14 RC oscillator for the ADC and wait for it
	RCC->CR2 |= RCC_CR2_HSI14ON;
	while (!(RCC->CR2 & RCC_CR2_HSI14RDY))
		;
}

void initTim1() {
	TIM1->CR2 |= TIM_CR2_MMS_1 | TIM_CR2_MMS_0; //output compare pulse - magic trigger needed for ADC
	TIM1->CR1 |= TIM_CR1_CEN | TIM_CR1_ARPE; // enable and auto-reload
	TIM1->ARR = 2399; //48mhz / 2400 = 20khz

	//Generate an update event to reload the Prescaler and the repetition countervalue immediately
	TIM1->EGR = TIM_EGR_UG;
}

void initAdc() {
	// set ADC_CR_ADCAL to start calibration and wait for it to clear
	ADC1->CR |= ADC_CR_ADCAL;
	while (ADC1->CR & ADC_CR_ADCAL)
		;
	//set ADC_CFGR1_CONT for continuous mode
	//set ADC_CFGR1_EXTEN = 01 for trigger on rising edge
	//set ADC_CFGR1_EXTSEL = 000 for TIM1_TRGO
	//set ADC_CFGR1_DMACFG = 1 for dma circular
	//set ADC_CFGR1_DMAEN = 1 to enable dma
	ADC1->CFGR1 = ADC_CFGR1_DMACFG | ADC_CFGR1_DMAEN | ADC_CFGR1_EXTEN_0;

	//set ADC_SMPR = 101 for 55cycle sample time
	ADC1->SMPR = 0b101;
	//set ADC_CHSELR bits to enable various channels (0, 1, 4, 6, 7, 8)
	ADC1->CHSELR = ADC_CHSELR_CHSEL0 | ADC_CHSELR_CHSEL1 | ADC_CHSELR_CHSEL4 |
					ADC_CHSELR_CHSEL5 | ADC_CHSELR_CHSEL6 | ADC_CHSELR_CHSEL7 | ADC_CHSELR_CHSEL9;
	//set ADC_ADEN to enable adc and wait for ready
	ADC1->CR |= ADC_CR_ADEN;
	while (!(ADC1->ISR & ADC_ISR_ADRDY))
		;
	//set ADC_CR_ADSTART to start (will wait for external trigger in this mode)
	ADC1->CR |= ADC_CR_ADSTART;
}

void initGpio() {
	//select alternate function 1 (usart1) for pin 2 and 3 - on the stm23f040f4 it defaults to TIM15_CH1/TIM15_CH2
	GPIOA->AFR[0] |= 0b0001 << 8 | 0b0001 << 12;
	//select alternate function 4 (i2c1) for pin 9 and 10
	GPIOA->AFR[1] |= 0b0100 << 4 | 0b0100 << 8;
	//set a0, a1, a4, a5, a6, a7 and b1 to analog inputs
	//set a2, a3 as alternate mode (tx, rx) and a9, a10 as alt mode (i2c)
	GPIOA->MODER |= GPIO_MODER_MODER0 | GPIO_MODER_MODER1 |
			GPIO_MODER_MODER2_1 | GPIO_MODER_MODER3_1 | //set up a2 and a3 as alt function (usart1)
			GPIO_MODER_MODER4 | GPIO_MODER_MODER5 | GPIO_MODER_MODER6 | GPIO_MODER_MODER7 |
			GPIO_MODER_MODER9_1 | GPIO_MODER_MODER10_1; //set up a9 and a10 as alt function (i2c)

	GPIOB->MODER |= GPIO_MODER_MODER1;
//	GPIOB->MODER |= GPIO_MODER_MODER1_0; //output for scoping timings
}

void initUart() {
	USART1->BRR = 0x1A1; // example from manual for 115200 @ 48mhz
	//enable tx, and uart, leave everything else 0 for 8N1
	USART1->CR1 = USART_CR1_TE | USART_CR1_UE;
	//enable DMA for transmits
	USART1->CR3 |= USART_CR3_DMAT;
}

void initDma() {
	//configure DMA to read from ADC into a buffer
	DMA1_Channel1->CPAR = (uint32_t) (&(ADC1->DR)); //point dma to ADC data reg
	DMA1_Channel1->CMAR = (uint32_t) (adcBuffer); //point DMA to buffer memory
	DMA1_Channel1->CNDTR = 7; //count of transfers per circle
	//enable DMA_CCR_CIRC (circular) mode (so addresses reset when its done)
	//set DMA_CCR_MINC to incrememnt memory address
	//set DMA_CCR_MSIZE = 01 for 16 bit xfers to memory
	//set DMA_CCR_PSIZE = 01 for 16 bit xfers from perepheral
	//set DMA_CCR_TCIE for transfer complete interrupt
	DMA1_Channel1->CCR |= DMA_CCR_CIRC | DMA_CCR_MINC | DMA_CCR_MSIZE_0 | DMA_CCR_PSIZE_0 | DMA_CCR_TCIE;
	DMA1_Channel1->CCR |= DMA_CCR_EN; //enable channel 1 dma

	NVIC_EnableIRQ(DMA1_Channel1_IRQn);
	NVIC_SetPriority(DMA1_Channel1_IRQn, 0);

	//NOTE DMA channel2 is used on the fly by sendOutBuffer()

	//configure DMA to read from ADC into a buffer
	DMA1_Channel3->CPAR = (uint32_t) (&(I2C1->RXDR)); //point dma to rx data reg
	DMA1_Channel3->CMAR = (uint32_t) (&accelerometer[0]); //point DMA to buffer memory
	DMA1_Channel3->CNDTR = 6; //count of transfers per circle
	//enable DMA_CCR_CIRC (circular) mode (so addresses reset when its done)
	//set DMA_CCR_MINC to incrememnt memory address
	//set DMA_CCR_TCIE for transfer complete interrupt
	DMA1_Channel3->CCR |= DMA_CCR_CIRC | DMA_CCR_MINC | DMA_CCR_TCIE;
	DMA1_Channel3->CCR |= DMA_CCR_EN; //enable channel 1 dma

	NVIC_EnableIRQ(DMA1_Channel2_3_IRQn);
	NVIC_SetPriority(DMA1_Channel2_3_IRQn, 0);

}

void initI2C() {
	I2C1->TIMINGR = (uint32_t) 0x0000020B; //magic value from cubemx for 400khz
	//enable the module and interrupts for any error condition
	I2C1->CR1 = I2C_CR1_PE | I2C_CR1_ERRIE | I2C_CR1_STOPIE | I2C_CR1_NACKIE | I2C_CR1_RXDMAEN;

	NVIC_EnableIRQ(I2C1_IRQn);
	NVIC_SetPriority(I2C1_IRQn, 4);
}


void writeToUsart(uint8_t * outBuffer, uint32_t len) {
	DMA1_Channel2->CCR = 0; //disable, reset state
	DMA1_Channel2->CPAR = (uint32_t) &USART1->TDR;
	DMA1_Channel2->CMAR = (uint32_t) outBuffer;
	DMA1_Channel2->CNDTR = len;
	//set DMA_CCR_MINC to increment mem address
	//set DMA_CCR_DIR for out to perepheral
	//set DMA_CCR_EN to make it so
	DMA1_Channel2->CCR = DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_EN;
}

//write a single value to a register. Slow, blocking, but usually only used during startup
//TODO could be more resilient to transient i2c errors. If things go really sideways this will just block forever
void i2cWriteReg(uint8_t addr, uint8_t reg, uint8_t value) {
	while (I2C1->ISR & I2C_ISR_BUSY)
		;
	//initiate start, send out slave address
	I2C_TransferHandling(I2C1, addr, 2, I2C_SoftEnd_Mode, I2C_Generate_Start_Write);
	//wait for TXIS
	while (I2C_GetFlagStatus(I2C1, I2C_ISR_TXIS) == 0)
		;
	//write out register address of interest
	I2C_SendData(I2C1, reg);
	while (I2C_GetFlagStatus(I2C1, I2C_ISR_TXIS) == 0)
		;
	I2C_SendData(I2C1, value);
	//wait for TC
	while (I2C_GetFlagStatus(I2C1, I2C_ISR_TC) == 0)
		;
	I2C_GenerateSTOP(I2C1, ENABLE);
}

void i2cReadReg(uint8_t addr, uint8_t reg, uint8_t * value, uint8_t len) {
	while (I2C1->ISR & I2C_ISR_BUSY)
		;
	//initiate start, send out slave address
	I2C_TransferHandling(I2C1, addr, 1, I2C_SoftEnd_Mode, I2C_Generate_Start_Write);
	//wait for TXIS
	while (I2C_GetFlagStatus(I2C1, I2C_ISR_TXIS) == 0)
		;
	//write out register address of interest
	I2C_SendData(I2C1, reg | 0x80);
	//wait for TC
	while (I2C_GetFlagStatus(I2C1, I2C_ISR_TC) == 0)
		;
	I2C_TransferHandling(I2C1, LIS3DH_ADDR, len, I2C_AutoEnd_Mode, I2C_Generate_Start_Read);

	for (int i = 0; i < len; i++) {
		//wait for RXNE
		while (I2C_GetFlagStatus(I2C1, I2C_ISR_RXNE) == 0)
			;
		value[i] = I2C_ReceiveData(I2C1);
	}

	while (I2C1->ISR & I2C_ISR_BUSY)
		;
}

void initAccelerometer() {
	I2C1->CR1 = I2C_CR1_PE | I2C_CR1_ERRIE | I2C_CR1_STOPIE | I2C_CR1_NACKIE | I2C_CR1_RXDMAEN;
	//configure registers for desired mode
	//set CTRL_REG1(20h) - ODR 100hz = 0101 | lpen off = 0 | xzy enable = 111
	i2cWriteReg(LIS3DH_ADDR, 0x20, 0b01010111);
	//set CTRL_REG4(23h) - BDU continuous = 1 | BLE big = 0 | FS 16g = 11 | HR high = 1 | ST off = 00 | SIM x = 0
	i2cWriteReg(LIS3DH_ADDR, 0x23, 0b10111000);
	I2CMode = IDLE;
}

//start poll sequence by initiating a start and sending the slave address
void startAccelerometerPoll() {
	//check to see if things are in the right state for this, or make it so
	//maybe something went wrong, and we can reset things back to normal.
	if (I2CMode == IDLE) {
		I2C_TransferHandling(I2C1, LIS3DH_ADDR, 1, I2C_SoftEnd_Mode, I2C_Generate_Start_Write);
		I2CMode = STARTING;
		I2C1->CR1 |= I2C_CR1_TXIE; //listen for TXIS, indicating that we can start writing
	} else {
		I2CMode = NEEDSRESET;
	}
}


void SysTick_Handler(void) {
	//keep track of milliseconds
	ms++;
}

//handle DMA for the channel doing ADC
void DMA1_CH1_IRQHandler() {
	if (DMA1->ISR & DMA_ISR_TCIF1) {
		//the dma transfer is complete
		int16_t audioSample = adcBuffer[0]<<3;


		//downsample 50:1 for the low frequency buffer
		bufferLowHz.avg += audioSample;
		if (++bufferLowHz.downSampleCounter >= 50) {
			bufferLowHz.downSampleCounter = 0;
			bufferLowHz.circular[bufferLowHz.head++] = bufferLowHz.avg/50 - (audioAverage>>16);
			bufferLowHz.avg = 0;
			if (bufferLowHz.head >= 32)
				bufferLowHz.head = 0;
		}

		volatile int32_t d = (audioSample<<16) - audioAverage;
		audioAverage += (d) >> 16;
		audioSample -= audioAverage>>16;


		//save to the ping-pong buffer
		buffer[readSide][readPos] = audioSample;

		readPos++;
		if (readPos >= HIGH_N) {
			//copy the 400 hz buffer snapshot
			for (int i = 0; i < LOW_N; i++) {
				bufferLowHz.output[i] = bufferLowHz.circular[(bufferLowHz.head + i) & 31];
			}
			//toggle sides and mark done
			readSide = !readSide;
			readPos = 0;
			readDone = true;
		}
	}

	//is this bad? this seems right, but also wrong somehow
	//unset any set bits for channel1
	DMA1->IFCR = DMA1->ISR & 0xf;
}


void I2C1_ErrorHandler() {
	I2CMode = NEEDSRESET;
	I2C_GenerateSTOP(I2C1, ENABLE);
}

void I2C1_IRQHandler() {
	if (I2C1->ISR & (I2C_ISR_NACKF | I2C_ISR_ARLO | I2C_ISR_BERR)) {
		I2C1_ErrorHandler();
	} else {
		switch (I2CMode) {
		case STARTING:
			//expect I2C_ISR_TXIS
			if (!(I2C1->ISR & I2C_ISR_TXIS)) {
				I2C1_ErrorHandler();
				break;
			}
			I2CMode = SENDING_REG;
			I2C_SendData(I2C1, 0x28 | 0x80); //start at OUT_X_L and set MSB to increment per read
			I2C1->CR1 &= ~I2C_CR1_TXIE; //ignore TXIS for now
			I2C1->CR1 |= I2C_CR1_TCIE; //listen for TC

			break;
		case SENDING_REG:
			//expect I2C_ISR_TC
			if (!(I2C1->ISR & I2C_ISR_TC)) {
				I2C1_ErrorHandler();
				break;
			}
			I2CMode = READING;
			I2C_TransferHandling(I2C1, LIS3DH_ADDR, 6, I2C_AutoEnd_Mode, I2C_Generate_Start_Read);
			I2C1->CR1 &= ~I2C_CR1_TCIE; //ignore TC for now, waiting for DMA
			break;
		case READING:
			//DMA is handling the reads, so if we get here there's probably an error
			I2C1_ErrorHandler();
			break;
		case STOPPING:
			//expect I2C_ISR_STOPF
			if (!(I2C1->ISR & I2C_ISR_STOPF)) {
				I2C1_ErrorHandler();
				return;
			}
			I2CMode = IDLE;
			break;
		case IDLE:
			//don't care really
			break;
		case NEEDSRESET:
			//can't do much here
			break;
		}
	}

	I2C1->ICR = I2C1->ISR; //is this bad? this seems right, but also wrong somehow
}

//handle DMA for the channel doing I2C
void DMA1_CH2_3_IRQHandler() {
	if (DMA1->ISR & DMA_ISR_TCIF3) {
		//i2c transfer complete
		I2CMode = STOPPING;
	}
	//is this bad? this seems right, but also wrong somehow
	//unset any set bits for channel3
	DMA1->IFCR = DMA1->ISR & 0xf00;
}

