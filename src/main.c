#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/printk.h>
#include <string.h>

/* ---------- UART1 usada para comunicação entre placas ---------- */
#define UART1_NODE DT_NODELABEL(uart1)
static const struct device *const uart_dev = DEVICE_DT_GET(UART1_NODE);

/* ---------- Configurações gerais ---------- */
#define MSG_SIZE 64

/* Fila de mensagens recebidas */
K_MSGQ_DEFINE(uart_msgq, MSG_SIZE, 10, 4);

/* Mutex que alterna TX/RX */
K_MUTEX_DEFINE(tx_rx_mutex);

/* Threads */
K_THREAD_STACK_DEFINE(tx_stack, 1024);
K_THREAD_STACK_DEFINE(rx_stack, 1024);

struct k_thread tx_thread_data;
struct k_thread rx_thread_data;

/* Buffer e posição para montagem da mensagem */
static char rx_buf[MSG_SIZE];
static int rx_pos = 0;


/* ======================================================= */
/*              CALLBACK COMPLETO DE RECEPÇÃO              */
/* ======================================================= */
/*
 * Agora funciona mesmo se o outro lado NÃO enviar '\n' ou '\r'.
 * Sempre que o buffer encher ou um terminador for recebido, gera uma mensagem.
 */
void serial_cb(const struct device *dev, void *user_data)
{
    uint8_t c;

    /* Lê FIFO até esvaziar */
    while (uart_fifo_read(uart_dev, &c, 1) == 1) {

        /* Armazena o byte */
        rx_buf[rx_pos++] = c;

        /* Condições de "mensagem completa": buffer cheio OU fim de linha */
        if (rx_pos >= MSG_SIZE - 1 || c == '\n' || c == '\r') {

            rx_buf[rx_pos] = '\0';

            /* Envia a mensagem completa para a fila */
            k_msgq_put(&uart_msgq, &rx_buf, K_NO_WAIT);

            /* Reinicia o buffer */
            rx_pos = 0;
        }
    }
}


/* ======================================================= */
/*                    Função de envio TX                   */
/* ======================================================= */
void uart1_send(const char *buf)
{
    for (int i = 0; buf[i] != '\0'; i++) {
        uart_poll_out(uart_dev, buf[i]);
    }
}


/* ======================================================= */
/*                       THREAD TX                         */
/* ======================================================= */
void tx_thread(void *a, void *b, void *c)
{
    while (1) {

        k_mutex_lock(&tx_rx_mutex, K_FOREVER);

        printk("\n[PLACA A / TX] Enviando por 5 segundos...\n");

        uint64_t start = k_uptime_get();

        while (k_uptime_get() - start < 5000) {

            /* Mensagem enviada pela UART1 */
            uart1_send("[A] Olá PLACA B! Mensagem da PLACA A via UART1\r\n");

            /* Mensagem também enviada ao monitor serial (UART0) */
            printk("[A → B] Mensagem enviada pela UART1.\n");

            k_sleep(K_MSEC(500));
        }

        printk("[PLACA A / TX] Finalizado. Alternando para RX.\n");

        k_mutex_unlock(&tx_rx_mutex);

        k_sleep(K_MSEC(100));
    }
}


/* ======================================================= */
/*                       THREAD RX                         */
/* ======================================================= */
void rx_thread(void *a, void *b, void *c)
{
    char msg[MSG_SIZE];

    while (1) {

        k_mutex_lock(&tx_rx_mutex, K_FOREVER);

        printk("\n[PLACA A / RX] Recebendo da PLACA B por 5 segundos...\n");

        uint64_t start = k_uptime_get();

        while (k_uptime_get() - start < 5000) {

            /* Tenta pegar mensagem da fila */
            if (k_msgq_get(&uart_msgq, &msg, K_MSEC(50)) == 0) {

                printk("[A ← B] Recebido pela UART1: %s\n", msg);
            }
        }

        printk("[PLACA A / RX] Finalizado. Alternando para TX.\n");

        k_mutex_unlock(&tx_rx_mutex);

        k_sleep(K_MSEC(100));
    }
}


/* ======================================================= */
/*                           MAIN                          */
/* ======================================================= */
int main(void)
{
    printk("=== Inicializando PLACA A (UART1) ===\n");

    if (!device_is_ready(uart_dev)) {
        printk("ERRO: UART1 não está pronta!\n");
        return 0;
    }

    /* Configura callback de RX */
    uart_irq_callback_user_data_set(uart_dev, serial_cb, NULL);

    /* Habilita interrupção de recepção */
    uart_irq_rx_enable(uart_dev);

    printk("UART1 configurada. Sistema iniciando...\n");

    /* Criação das threads */
    k_thread_create(&tx_thread_data, tx_stack, K_THREAD_STACK_SIZEOF(tx_stack),
                    tx_thread, NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);

    k_thread_create(&rx_thread_data, rx_stack, K_THREAD_STACK_SIZEOF(rx_stack),
                    rx_thread, NULL, NULL, NULL,
                    5, 0, K_NO_WAIT);

    return 0;
}
