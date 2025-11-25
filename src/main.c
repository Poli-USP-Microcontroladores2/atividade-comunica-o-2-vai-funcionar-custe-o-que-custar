#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <string.h>

#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)
#define MSG_SIZE 32

K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

/* Mutex para garantir exclusão mútua entre TX e RX */
K_MUTEX_DEFINE(tx_rx_mutex);

/* Thread stacks */
K_THREAD_STACK_DEFINE(tx_stack, 1024);
K_THREAD_STACK_DEFINE(rx_stack, 1024);

/* Thread data */
struct k_thread tx_thread_data;
struct k_thread rx_thread_data;

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

static char rx_buf[MSG_SIZE];
static int rx_buf_pos;

/* --- CALLBACK UART RX ISR --- */
void serial_cb(const struct device *dev, void *user_data)
{
    uint8_t c;

    if (!uart_irq_update(uart_dev)) {
        return;
    }

    if (!uart_irq_rx_ready(uart_dev)) {
        return;
    }

    while (uart_fifo_read(uart_dev, &c, 1) == 1) {
        if ((c == '\n' || c == '\r') && rx_buf_pos > 0) {
            rx_buf[rx_buf_pos] = '\0';
            k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);
            rx_buf_pos = 0;
        } else if (rx_buf_pos < MSG_SIZE - 1) {
            rx_buf[rx_buf_pos++] = c;
        }
    }
}

/* --- Função de envio UART --- */
void print_uart(char *buf)
{
    int msg_len = strlen(buf);
    for (int i = 0; i < msg_len; i++) {
        uart_poll_out(uart_dev, buf[i]);
    }
}

/* ----------------------------------------- */
/* THREAD DE ENVIO (THREAD TX)               */
/* ----------------------------------------- */
void tx_thread(void *a, void *b, void *c)
{
    while (1) {
        k_mutex_lock(&tx_rx_mutex, K_FOREVER);   // Exclusão mútua

        uint64_t start = k_uptime_get();

        print_uart("\r\n[TX] Iniciando envio por 5 segundos...\r\n");

        while (k_uptime_get() - start < 5000) {
            print_uart("[TX] Mensagem enviada da placa A\r\n");
            k_sleep(K_MSEC(500));
        }

        print_uart("[TX] Finalizado. Liberando acesso...\r\n");

        k_mutex_unlock(&tx_rx_mutex);

        k_sleep(K_MSEC(100));  // pequeno delay antes da próxima rodada
    }
}

/* ----------------------------------------- */
/* THREAD DE RECEPÇÃO (THREAD RX)            */
/* ----------------------------------------- */
void rx_thread(void *a, void *b, void *c)
{
    char msg[MSG_SIZE];

    while (1) {
        k_mutex_lock(&tx_rx_mutex, K_FOREVER);   // Espera TX liberar

        uint64_t start = k_uptime_get();

        print_uart("\r\n[RX] Iniciando recepção por 5 segundos...\r\n");

        while (k_uptime_get() - start < 5000) {

            if (k_msgq_get(&uart_msgq, &msg, K_MSEC(50)) == 0) {
                print_uart("[RX] Recebido: ");
                print_uart(msg);
                print_uart("\r\n");
            }
        }

        print_uart("[RX] Finalizado. Liberando acesso...\r\n");

        k_mutex_unlock(&tx_rx_mutex);

        k_sleep(K_MSEC(100));
    }
}

/* ----------------------------------------- */
/* MAIN                                      */
/* ----------------------------------------- */
int main(void)
{
    if (!device_is_ready(uart_dev)) {
        printk("UART device not found!\n");
        return 0;
    }

    /* configurar callback de RX */
    uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
    uart_irq_rx_enable(uart_dev);

    print_uart("Sistema TX/RX com threads + mutex iniciado.\r\n");

    /* Criação das threads */
    k_thread_create(&tx_thread_data, tx_stack, K_THREAD_STACK_SIZEOF(tx_stack),
        tx_thread, NULL, NULL, NULL,
        5, 0, K_NO_WAIT);

    k_thread_create(&rx_thread_data, rx_stack, K_THREAD_STACK_SIZEOF(rx_stack),
        rx_thread, NULL, NULL, NULL,
        5, 0, K_NO_WAIT);

    return 0;
}