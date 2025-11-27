/*
 * main.c  --  PLACA B (FRDM KL25Z)
 *
 * Comportamento:
 *  - Inicia em modo RX (5s) com LED verde ligado
 *  - Depois alterna para TX (5s) enviando mensagens pela UART1
 *  - Repete o ciclo indefinidamente
 *
 * Observações:
 *  - Comunicação entre placas via UART1 (DT nodelabel: uart1)
 *  - Monitor serial permanece UART0 (printk)
 *  - Callback de RX montando linhas/frames mesmo sem CR/LF
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <string.h>

/* ---------- UART1 ---------- */
#define UART1_NODE DT_NODELABEL(uart1)
static const struct device *const uart_dev = DEVICE_DT_GET(UART1_NODE);

/* ---------- LED (opcional) ---------- */
#ifdef DT_ALIAS_LED0
#define LED0_NODE DT_ALIAS(led0)
static const struct device *led_dev = DEVICE_DT_GET(DT_GPIO_CTLR(LED0_NODE, gpios));
static const gpio_pin_t led_pin = DT_GPIO_PIN(LED0_NODE, gpios);
static const gpio_flags_t led_flags = DT_GPIO_FLAGS(LED0_NODE, gpios);
#else
static const struct device *led_dev = NULL;
static const gpio_pin_t led_pin = 0;
static const gpio_flags_t led_flags = 0;
#endif

/* ---------- Mensagens / fila / mutex / threads ---------- */
#define MSG_SIZE 64
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

K_MUTEX_DEFINE(tx_rx_mutex);

/* Stacks */
K_THREAD_STACK_DEFINE(tx_stack, 1024);
K_THREAD_STACK_DEFINE(rx_stack, 1024);

/* Thread control */
struct k_thread tx_thread_data;
struct k_thread rx_thread_data;

/* Buffer de montagem */
static char rx_buf[MSG_SIZE];
static int rx_pos = 0;

/* ---------- Callback UART RX (ISR) ---------- */
void serial_cb(const struct device *dev, void *user_data)
{
    uint8_t c;

    /* lê FIFO até esvaziar */
    while (uart_fifo_read(uart_dev, &c, 1) == 1) {
        rx_buf[rx_pos++] = c;

        /* Se receber terminador ou buffer encher -> envia para fila */
        if (rx_pos >= MSG_SIZE - 1 || c == '\n' || c == '\r') {
            rx_buf[rx_pos] = '\0';
            /* tenta colocar na fila sem bloquear; se falhar, descartamos a mensagem */
            k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);
            rx_pos = 0;
        }
    }
}

/* ---------- Envio via UART1 ---------- */
void uart1_send(const char *buf)
{
    for (int i = 0; buf[i] != '\0'; i++) {
        uart_poll_out(uart_dev, buf[i]);
    }
}

/* ---------- Helpers LED ---------- */
static void led_init_if_available(void)
{
#ifdef DT_ALIAS_LED0
    if (device_is_ready(led_dev)) {
        gpio_pin_configure(led_dev, led_pin, GPIO_OUTPUT_INACTIVE | led_flags);
        gpio_pin_set(led_dev, led_pin, 0);
    } else {
        printk("Aviso: led0 não está pronto; continuando sem LED.\n");
        led_dev = NULL;
    }
#else
    led_dev = NULL;
#endif
}

static void led_set_on(void)
{
    if (led_dev && device_is_ready(led_dev)) {
        gpio_pin_set(led_dev, led_pin, 1);
    }
}

static void led_set_off(void)
{
    if (led_dev && device_is_ready(led_dev)) {
        gpio_pin_set(led_dev, led_pin, 0);
    }
}

/* ---------- THREAD TX (envia por 5s) ---------- */
void tx_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    while (1) {
        /* tenta pegar exclusão para TX */
        k_mutex_lock(&tx_rx_mutex, K_FOREVER);

        /* Indica modo TX */
        led_set_off();

        printk("\n[PLACA B / TX] Enviando pela UART1 por 5s...\n");

        uint64_t start = k_uptime_get();
        while (k_uptime_get() - start < 5000) {
            const char msg[] = "[B] Oi A! Mensagem da PLACA B via UART1\r\n";
            uart1_send(msg);
            printk("[B → A] Mensagem enviada pela UART1.\n");
            k_sleep(K_MSEC(500));
        }

        printk("[PLACA B / TX] Envio finalizado.\n");

        k_mutex_unlock(&tx_rx_mutex);

        k_sleep(K_MSEC(100));
    }
}

/* ---------- THREAD RX (recebe por 5s) ---------- */
void rx_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    char msg[MSG_SIZE];

    while (1) {
        /* aguarda disponibilidade para RX */
        k_mutex_lock(&tx_rx_mutex, K_FOREVER);

        /* Indica modo RX */
        led_set_on();

        printk("\n[PLACA B / RX] Recebendo da PLACA A por 5s (LED verde ON)...\n");

        uint64_t start = k_uptime_get();
        while (k_uptime_get() - start < 5000) {
            if (k_msgq_get(&uart_msgq, &msg, K_MSEC(50)) == 0) {
                /* imprime a mensagem recebida no monitor serial */
                printk("[B ← A] Recebido pela UART1: %s\n", msg);
            }
        }

        printk("[PLACA B / RX] Recepção finalizada (LED verde OFF).\n");
        led_set_off();

        k_mutex_unlock(&tx_rx_mutex);

        k_sleep(K_MSEC(100));
    }
}

/* ---------- MAIN ---------- */
int main(void)
{
    printk("=== Inicializando PLACA B (UART1) ===\n");

    if (!device_is_ready(uart_dev)) {
        printk("ERRO: UART1 não está pronta!\n");
        return 0;
    }

    /* Inicializa LED se possível */
    led_init_if_available();

    /* Configura callback e habilita RX IRQ */
    uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);
    uart_irq_rx_enable(uart_dev);

    printk("UART1 configurada. Criando threads (RX com prioridade maior para iniciar em RX)...\n");

    /* Criar threads: RX com prioridade maior (4) para garantir que inicie em RX */
    k_thread_create(&rx_thread_data, rx_stack, K_THREAD_STACK_SIZEOF(rx_stack),
                    rx_thread, NULL, NULL, NULL,
                    4, 0, K_NO_WAIT);

    k_thread_create(&tx_thread_data, tx_stack, K_THREAD_STACK_SIZEOF(tx_stack),
                    tx_thread, NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);

    return 0;
}
