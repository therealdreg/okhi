#ifndef POC_H
#define POC_H

#ifdef __cplusplus
extern "C" {
#endif

// Buffer para almacenar datos y variables para manejar el buffer
#define BUFFER_SIZE 256*100
extern volatile unsigned char buffer[BUFFER_SIZE];
extern volatile unsigned int bufferIndex;

    void spi_task(void *pvParameters);
#ifdef __cplusplus
}
#endif

#endif // POC_H
