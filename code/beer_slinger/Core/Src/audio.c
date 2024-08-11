#include "audio.h"
#include <string.h>

#define GW_AUDIO_FREQUENCY 32768

uint32_t audio_mute;
int16_t audiobuffer_dma[AUDIO_BUFFER_LENGTH * 2] __attribute__((section(".audio")));
dma_transfer_state_t dma_state;
uint32_t dma_counter;
static uint16_t audiobuffer_length = AUDIO_BUFFER_LENGTH;
// Parameters for the square wave
static uint16_t square_wave_length_ms;
static uint16_t square_wave_frequency;
static uint16_t square_wave_volume;
static uint32_t square_wave_samples_remaining;
static uint32_t square_wave_phase;

static bool musicOn = true;

static int volumeDisplayed = 50;		//Shows 0-100 on screen, this is *2 for the actual output

void HAL_SAI_TxHalfCpltCallback(SAI_HandleTypeDef *hsai) {
#ifdef SALEAE_DEBUG_SIGNALS
    HAL_GPIO_WritePin(DEBUG_PORT_PIN_0, GPIO_PIN_SET);
#endif
    dma_counter++;
    dma_state = DMA_TRANSFER_STATE_HF;
    audio_generate_square_wave_chunk();
}

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai) {
#ifdef SALEAE_DEBUG_SIGNALS
    HAL_GPIO_WritePin(DEBUG_PORT_PIN_0, GPIO_PIN_RESET);
#endif
    dma_counter++;
    dma_state = DMA_TRANSFER_STATE_TC;
    audio_generate_square_wave_chunk();
}

void musicControl(bool state) {
	musicOn = state;
}

void volumeUp() {

	volumeDisplayed += 5;

	if (volumeDisplayed > 100) {
		volumeDisplayed = 100;
	}

}

void volumeDown() {

	volumeDisplayed -= 5;

	if (volumeDisplayed < 0) {
		volumeDisplayed = 0;
	}

}

int getVolume() {
	return volumeDisplayed;
}


void audio_set_frequency() {
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    PeriphClkInitStruct.PLL2.PLL2M = 25;
    PeriphClkInitStruct.PLL2.PLL2N = 196;
    PeriphClkInitStruct.PLL2.PLL2P = 10;
    PeriphClkInitStruct.PLL2.PLL2FRACN = 5000;

    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_SAI1;
    PeriphClkInitStruct.Sai1ClockSelection = RCC_SAI1CLKSOURCE_PLL2;
    PeriphClkInitStruct.AdcClockSelection = RCC_ADCCLKSOURCE_PLL2;

    PeriphClkInitStruct.PLL2.PLL2Q = 2;
    PeriphClkInitStruct.PLL2.PLL2R = 5;
    PeriphClkInitStruct.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_1;
    PeriphClkInitStruct.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;

    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
        Error_Handler();
    }

    HAL_SAI_DeInit(&hsai_BlockA1);
    hsai_BlockA1.Init.AudioFrequency = GW_AUDIO_FREQUENCY;
    HAL_SAI_Init(&hsai_BlockA1);
}

uint16_t audio_get_buffer_length() {
    return audiobuffer_length;
}

uint16_t audio_get_buffer_size() {
    return audio_get_buffer_length() * sizeof(int16_t);
}

int16_t *audio_get_active_buffer(void) {
    size_t offset = (dma_state == DMA_TRANSFER_STATE_HF) ? 0 : audiobuffer_length;
    return &audiobuffer_dma[offset];
}

int16_t *audio_get_inactive_buffer(void) {
    size_t offset = (dma_state == DMA_TRANSFER_STATE_TC) ? 0 : audiobuffer_length;
    return &audiobuffer_dma[offset];
}

void audio_clear_active_buffer() {
    memset(audio_get_active_buffer(), 0, audiobuffer_length * sizeof(audiobuffer_dma[0]));
}

void audio_clear_inactive_buffer() {
    memset(audio_get_inactive_buffer(), 0, audiobuffer_length * sizeof(audiobuffer_dma[0]));
}

void audio_clear_buffers() {
    memset(audiobuffer_dma, 0, sizeof(audiobuffer_dma));
}

void audio_set_buffer_length(uint16_t length) {
    audiobuffer_length = length;
}

void audio_generate_square_wave_chunk() {
    int16_t *inactive_buffer = audio_get_inactive_buffer();
    uint32_t samples_to_copy = audiobuffer_length;
    uint32_t samples_per_cycle = GW_AUDIO_FREQUENCY / square_wave_frequency;
    uint32_t high_samples = samples_per_cycle / 2;
    uint32_t low_samples = samples_per_cycle - high_samples;

    if (square_wave_samples_remaining < samples_to_copy) {
        samples_to_copy = square_wave_samples_remaining;
    }

    for (uint32_t i = 0; i < samples_to_copy; i++) {
        if ((square_wave_phase % samples_per_cycle) < high_samples) {
            inactive_buffer[i] = square_wave_volume;
        } else {
            inactive_buffer[i] = -square_wave_volume;
        }
        square_wave_phase++;
    }

    square_wave_samples_remaining -= samples_to_copy;

    if (square_wave_samples_remaining == 0) {
        audio_stop_playing();
    }
}

void squareWave(uint16_t length_ms, uint16_t frequency, uint8_t volume) {

	if (musicOn == false) {
		return;
	}

    square_wave_length_ms = length_ms;
    square_wave_frequency = frequency;
    square_wave_volume = volume * (volumeDisplayed * 2);		//Max * 200
    square_wave_samples_remaining = (length_ms * GW_AUDIO_FREQUENCY) / 1000;
    audio_start_playing(AUDIO_BUFFER_LENGTH);
    audio_generate_square_wave_chunk();
}

void audio_start_playing(uint16_t length) {
    audio_clear_buffers();
    audio_set_buffer_length(length);
    HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)audiobuffer_dma, audiobuffer_length * 2);
}

void audio_stop_playing() {
    audio_clear_buffers();
    HAL_SAI_DMAStop(&hsai_BlockA1);
}
