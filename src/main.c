/*
 * main.c — Placa B
 * Comunicação UART half-duplex em turnos usando threads + mutex + ISR
 *
 * Comportamento da placa B:
 *   - Primeiro entra em MODO RECEPÇÃO por 5s
 *   - Depois entra em MODO TRANSMISSÃO por 5s
 *
 *   Isso é o inverso da placa A, permitindo troca de mensagens em turnos.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <errno.h>

#define MSG_SIZE 32

#ifndef DT_CHOSEN_zephyr_shell_uart
#error "DT_CHOSEN(zephyr_shell_uart) não está definido para esta placa."
#endif

#define UART_DEVICE_NODE DT_CHOSEN(zephyr_shell_uart)

/* --- Objetos globais --- */
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);
K_MUTEX_DEFINE(tx_rx_mutex);

K_THREAD_STACK_DEFINE(tx_stack, 1536);
K_THREAD_STACK_DEFINE(rx_stack, 1536);
static struct k_thread tx_thread_data;
static struct k_thread rx_thread_data;

static const struct device *const uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

static char rx_buf[MSG_SIZE];
static int rx_buf_pos = 0;

/* ---------- Função de envio ---------- */
static void print_uart(const char *buf)
{
    if (!buf || !uart_dev) return;

    for (size_t i = 0; i < strlen(buf); i++) {
        uart_poll_out(uart_dev, buf[i]);
    }
}

/* ---------- ISR UART ---------- */
void serial_cb(const struct device *dev, void *user_data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(user_data);

    uint8_t c;

    if (!uart_irq_update(uart_dev)) return;
    if (!uart_irq_rx_ready(uart_dev)) return;

    while (uart_fifo_read(uart_dev, &c, 1) == 1) {

        if ((c == '\n' || c == '\r') && rx_buf_pos > 0) {
            rx_buf[rx_buf_pos] = '\0';

            (void)k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);

            rx_buf_pos = 0;
        }
        else if (rx_buf_pos < (MSG_SIZE - 1)) {
            rx_buf[rx_buf_pos++] = (char)c;
        }
    }
}

/* ---------- THREAD RX (Placa B começa por aqui) ---------- */
void rx_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    char msg[MSG_SIZE];

    while (1) {
        k_mutex_lock(&tx_rx_mutex, K_FOREVER);

        print_uart("\r\n[RX/B] Modo RECEPÇÃO - 5s\r\n");

        uint64_t start = k_uptime_get();

        while ((k_uptime_get() - start) < 5000) {
            int ret = k_msgq_get(&uart_msgq, &msg, K_MSEC(100));

            if (ret == 0) {
                print_uart("[RX/B] Recebido: ");
                print_uart(msg);
                print_uart("\r\n");
            }
        }

        print_uart("[RX/B] Recepção finalizada — liberando mutex\r\n");
        k_mutex_unlock(&tx_rx_mutex);

        k_sleep(K_MSEC(100));
    }
}

/* ---------- THREAD TX (executa após RX) ---------- */
void tx_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);

    while (1) {
        k_mutex_lock(&tx_rx_mutex, K_FOREVER);

        print_uart("\r\n[TX/B] Modo TRANSMISSÃO - 5s\r\n");

        uint64_t start = k_uptime_get();
        while ((k_uptime_get() - start) < 5000) {

            print_uart("[TX/B] Mensagem da placa B\r\n");

            k_sleep(K_MSEC(500));
        }

        print_uart("[TX/B] Transmissão finalizada — liberando mutex\r\n");
        k_mutex_unlock(&tx_rx_mutex);

        k_sleep(K_MSEC(100));
    }
}

/* ---------- MAIN ----------- */
int main(void)
{
    int ret;

#ifdef UART_DEVICE_NODE
#else
    printk("Erro: nó UART não definido.\n");
    return -ENODEV;
#endif

    if (uart_dev == NULL) {
        printk("Erro: uart_dev == NULL.\n");
        return -ENODEV;
    }

    if (!device_is_ready(uart_dev)) {
        printk("Erro: driver UART não está pronto.\n");
        return -EIO;
    }

#ifndef CONFIG_UART_INTERRUPT_DRIVEN
    printk("Erro: CONFIG_UART_INTERRUPT_DRIVEN deve estar habilitado.\n");
    return -ENOTSUP;
#endif

    ret = uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
    if (ret < 0) {
        printk("Erro ao instalar callback UART: %d\n", ret);
        return ret;
    }

    uart_irq_rx_enable(uart_dev);

    /* Pequeno teste da fila */
    {
        char tput[MSG_SIZE] = "testB";
        char tget[MSG_SIZE] = {0};

        if (k_msgq_put(&uart_msgq, &tput, K_NO_WAIT) == 0) {
            k_msgq_get(&uart_msgq, &tget, K_NO_WAIT);
        }
    }

    rx_buf_pos = 0;

    /* Placa B inicia primeiro no RX */
    k_thread_create(&rx_thread_data, rx_stack,
                    K_THREAD_STACK_SIZEOF(rx_stack),
                    rx_thread, NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);

    k_thread_create(&tx_thread_data, tx_stack,
                    K_THREAD_STACK_SIZEOF(tx_stack),
                    tx_thread, NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);

    printk("Placa B iniciada com sucesso (RX → TX turnos).\n");
    print_uart("Placa B iniciada com sucesso (RX → TX turnos).\r\n");

    return 0;
}
