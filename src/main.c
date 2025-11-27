#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <string.h>

/* ---------- CONFIGURAÇÃO UART1 PARA COMUNICAÇÃO ENTRE PLACAS ---------- */
#define UART1_NODE DT_NODELABEL(uart1)
static const struct device *const uart_dev = DEVICE_DT_GET(UART1_NODE);

/* Tamanho da mensagem */
#define MSG_SIZE 32

/* Fila de mensagens recebidas */
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

/* Mutex para alternar TX e RX */
K_MUTEX_DEFINE(tx_rx_mutex);

/* Thread stacks */
K_THREAD_STACK_DEFINE(tx_stack, 1024);
K_THREAD_STACK_DEFINE(rx_stack, 1024);

/* Thread data */
struct k_thread tx_thread_data;
struct k_thread rx_thread_data;

/* Buffer temporário para recepção */
static char rx_buf[MSG_SIZE];
static int rx_buf_pos = 0;

/* ======================================================== */
/*            INTERRUPÇÃO DE RECEPÇÃO (UART1 RX)            */
/* ======================================================== */
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
        } 
        else if (rx_buf_pos < MSG_SIZE - 1) {
            rx_buf[rx_buf_pos++] = c;
        }
    }
}

/* ======================================================== */
/*                 Função auxiliar de envio                 */
/* ======================================================== */
void uart1_send(char *buf)
{
    int len = strlen(buf);
    for (int i = 0; i < len; i++) {
        uart_poll_out(uart_dev, buf[i]);
    }
}

/* ======================================================== */
/*                   THREAD DE ENVIO – TX                   */
/* ======================================================== */
void tx_thread(void *a, void *b, void *c)
{
    while (1) {

        k_mutex_lock(&tx_rx_mutex, K_FOREVER);

        uint64_t start = k_uptime_get();

        printk("\n[TX] Iniciando envio pela UART1 por 5s...\n");

        while (k_uptime_get() - start < 5000) {
            uart1_send("[A] Mensagem enviada pela UART1\n");
            printk("[MONITOR] Placa A enviou pela UART1\n");
            k_sleep(K_MSEC(500));
        }

        printk("[TX] Envio finalizado.\n");

        k_mutex_unlock(&tx_rx_mutex);

        k_sleep(K_MSEC(100));
    }
}

/* ======================================================== */
/*                THREAD DE RECEPÇÃO – RX                   */
/* ======================================================== */
void rx_thread(void *a, void *b, void *c)
{
    char msg[MSG_SIZE];

    while (1) {

        k_mutex_lock(&tx_rx_mutex, K_FOREVER);

        uint64_t start = k_uptime_get();

        printk("\n[RX] Modo recepção pela UART1 por 5s...\n");

        while (k_uptime_get() - start < 5000) {

            if (k_msgq_get(&uart_msgq, &msg, K_MSEC(50)) == 0) {
                printk("[RX] Recebido pela UART1: %s\n", msg);
            }
        }

        printk("[RX] Recepção finalizada.\n");

        k_mutex_unlock(&tx_rx_mutex);

        k_sleep(K_MSEC(100));
    }
}

/* ======================================================== */
/*                           MAIN                           */
/* ======================================================== */
int main(void)
{
    printk("Inicializando PLACA A com UART1...\n");

    if (!device_is_ready(uart_dev)) {
        printk("ERRO: UART1 não está pronta!\n");
        return 0;
    }

    /* Configurar callback e habilitar RX */
    uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
    uart_irq_rx_enable(uart_dev);

    printk("UART1 configurada com sucesso.\n");

    /* Criar threads */
    k_thread_create(&tx_thread_data, tx_stack, K_THREAD_STACK_SIZEOF(tx_stack),
        tx_thread, NULL, NULL, NULL, 5, 0, K_NO_WAIT);

    k_thread_create(&rx_thread_data, rx_stack, K_THREAD_STACK_SIZEOF(rx_stack),
        rx_thread, NULL, NULL, NULL, 5, 0, K_NO_WAIT);

    return 0;
}
