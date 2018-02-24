/**
 ******************************************************************************
 * @file    main.c
 * @author  Ac6
 * @version V1.0
 * @date    01-December-2013
 * @brief   Default main function.
 ******************************************************************************
 */

#include "stm32f0xx.h"
#include "stdio.h"
#include "stdbool.h"

#define N 512
#define NLOG2 9

extern int fix_fft(short fr[], short fi[], short m, short inverse);
extern uint16_t isqrt(uint32_t x);

void initRcc();
void initTim1();
void initAdc();
void initGpio();
void initDma();

volatile uint16_t adcBuffer[7]; //updated by DMA @ 20KHz
volatile uint32_t ms = 0; //updated by SysTick

volatile bool readDone;
int readSide;
int readPos;
int16_t buffer[2][N]; //ping-pong buffer

int main(void) {
	SysTick_Config(SystemCoreClock/1000); //tick interval 1ms
	initRcc();
	initTim1();
	initDma();
	initAdc();
	initGpio();

	for (;;) {
		//listen for message that the sample buffer is ready to process
		if (readDone) {
			readDone = false;

			GPIO_WriteBit(GPIOB, GPIO_Pin_1, 1);

			int16_t imag[N];
			for (int i = 0; i < N; i++)
				imag[i] = 0;

			//grab the side we're not currently writing to and do an FFT on it
			int16_t *buf = &buffer[!readSide][0];
			fix_fft(buf, &imag[0], NLOG2, 0);
			//calculate magnitude
			for (int i = 0; i < N/2; i++)
				imag[i] = isqrt(imag[i]*imag[i] + buf[i]*buf[i]);

			GPIO_WriteBit(GPIOB, GPIO_Pin_1, 0);
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
	TIM1->CR2 |= TIM_CR2_MMS_1 | TIM_CR2_MMS_0; //compare pulse - magic trigger needed for ADC
	TIM1->CR1 |= TIM_CR1_CEN | TIM_CR1_ARPE; // enable and auto-reload
	TIM1->ARR = 2399; //48mhz / 2400 = 20khz

	//Generate an update event to reload the Prescaler and the repetition countervalue immediatly
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
	//set a0, 2, 4, 5, 6, 7 and b1 to analog inputs
	GPIOA->MODER |= GPIO_MODER_MODER0 | GPIO_MODER_MODER1 | GPIO_MODER_MODER4 |
					GPIO_MODER_MODER5 | GPIO_MODER_MODER6 | GPIO_MODER_MODER7;
//	GPIOB->MODER |= GPIO_MODER_MODER1;
	GPIOB->MODER |= GPIO_MODER_MODER1_0; //output
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
}

void SysTick_Handler(void) {
	//keep track of milliseconds
	ms++;
}

void DMA1_CH1_IRQHandler() {
	if (DMA1->ISR & DMA_ISR_TCIF1) {
		//the dma transfer is complete, save off the audio channel to a ping-pong buffer
		buffer[readSide][readPos] = adcBuffer[0];
		readPos++;
		if (readPos >= N) {
			readSide = !readSide;
			readPos = 0;
			readDone = true;
		}
	}

	DMA1->IFCR = DMA1->ISR; //is this bad? this seems right, but also wrong somehow
}

