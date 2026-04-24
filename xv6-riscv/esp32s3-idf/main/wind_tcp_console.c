#include "sdkconfig.h"

#if CONFIG_WIND_WIFI_TCP_CONSOLE_ENABLE

#include <errno.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_rom_sys.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "lwip/sockets.h"
#include "lwip/inet.h"

#define RXQ_LEN 512
#define LISTEN_BACKLOG 1
#define WIFI_CONNECT_TIMEOUT_MS 20000

static const char *TAG = "wind_net";

static QueueHandle_t s_rxq;
static volatile int s_client_fd = -1;
static portMUX_TYPE s_client_lock = portMUX_INITIALIZER_UNLOCKED;
static EventGroupHandle_t s_wifi_event_group;
static esp_event_handler_instance_t s_any_wifi_instance;
static esp_event_handler_instance_t s_got_ip_instance;
static const int WIFI_CONNECTED_BIT = BIT0;

static void
net_log_boot(const char *msg)
{
  ESP_LOGI(TAG, "%s", msg);
  esp_rom_printf("wind_net: %s\n", msg);
}

static void
net_set_client_fd(int fd)
{
  portENTER_CRITICAL(&s_client_lock);
  s_client_fd = fd;
  portEXIT_CRITICAL(&s_client_lock);
}

static int
net_get_client_fd(void)
{
  int fd;
  portENTER_CRITICAL(&s_client_lock);
  fd = s_client_fd;
  portEXIT_CRITICAL(&s_client_lock);
  return fd;
}

static void
net_close_client(void)
{
  int fd = net_get_client_fd();
  if(fd >= 0)
    close(fd);
  net_set_client_fd(-1);
}

static void
wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
  (void)arg;

  if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START){
    net_log_boot("wifi sta start; connecting...");
    esp_wifi_connect();
    return;
  }

  if(event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED){
    net_log_boot("wifi disconnected; retrying");
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    esp_wifi_connect();
    return;
  }

  if(event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP){
    ip_event_got_ip_t *got_ip = (ip_event_got_ip_t *)event_data;
    if(got_ip != NULL){
      ESP_LOGI(TAG, "wifi got ip: " IPSTR, IP2STR(&got_ip->ip_info.ip));
      esp_rom_printf("wind_net: wifi got ip: " IPSTR "\n", IP2STR(&got_ip->ip_info.ip));
    }
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
  }
}

static void
wind_tcp_server_task(void *arg)
{
  int listen_fd;
  struct sockaddr_in addr;
  int yes = 1;

  (void)arg;

  listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
  if(listen_fd < 0){
    ESP_LOGE(TAG, "socket() failed errno=%d", errno);
    vTaskDelete(NULL);
    return;
  }

  setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(CONFIG_WIND_TCP_CONSOLE_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  if(bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0){
    ESP_LOGE(TAG, "bind() failed errno=%d", errno);
    esp_rom_printf("wind_net: bind failed errno=%d\n", errno);
    close(listen_fd);
    vTaskDelete(NULL);
    return;
  }

  if(listen(listen_fd, LISTEN_BACKLOG) != 0){
    ESP_LOGE(TAG, "listen() failed errno=%d", errno);
    esp_rom_printf("wind_net: listen failed errno=%d\n", errno);
    close(listen_fd);
    vTaskDelete(NULL);
    return;
  }

  ESP_LOGI(TAG, "TCP console listening on port %d", CONFIG_WIND_TCP_CONSOLE_PORT);
  esp_rom_printf("wind_net: tcp console listening on port %d\n", CONFIG_WIND_TCP_CONSOLE_PORT);

  for(;;){
    int client_fd;
    struct timeval tv;
    struct sockaddr_in peer;
    socklen_t peer_len = sizeof(peer);

    client_fd = accept(listen_fd, (struct sockaddr *)&peer, &peer_len);
    if(client_fd < 0)
      continue;

    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    net_set_client_fd(client_fd);
    ESP_LOGI(TAG, "TCP client connected from %s:%d", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
    esp_rom_printf("wind_net: tcp client connected from %s:%d\n", inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));

    for(;;){
      uint8_t buf[64];
      int n;
      int i;

      n = recv(client_fd, buf, sizeof(buf), 0);
      if(n > 0){
        for(i = 0; i < n; i++){
          uint8_t c = buf[i];
          (void)xQueueSend(s_rxq, &c, 0);
        }
        continue;
      }

      if(n == 0)
        break;

      if(errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR)
        continue;
      break;
    }

    net_close_client();
    ESP_LOGI(TAG, "TCP console client disconnected");
    esp_rom_printf("wind_net: tcp client disconnected\n");
  }
}

void
wind_net_console_start(void)
{
  wifi_init_config_t wifi_init_cfg = WIFI_INIT_CONFIG_DEFAULT();
  wifi_config_t sta_cfg = {
    .sta = {
      .ssid = CONFIG_WIND_WIFI_SSID,
      .password = CONFIG_WIND_WIFI_PASSWORD,
      .scan_method = WIFI_ALL_CHANNEL_SCAN,
      .sort_method = WIFI_CONNECT_AP_BY_SIGNAL,
      .failure_retry_cnt = 10,
    },
  };
  EventBits_t bits;
  esp_err_t err;

  s_rxq = xQueueCreate(RXQ_LEN, sizeof(uint8_t));
  if(s_rxq == NULL){
    ESP_LOGE(TAG, "rx queue create failed");
    esp_rom_printf("wind_net: rx queue create failed\n");
    return;
  }

  net_log_boot("starting wifi tcp console");
  esp_rom_printf("wind_net: target ssid='%s' port=%d\n", CONFIG_WIND_WIFI_SSID, CONFIG_WIND_TCP_CONSOLE_PORT);

  err = nvs_flash_init();
  if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND){
    (void)nvs_flash_erase();
    err = nvs_flash_init();
  }
  if(err != ESP_OK){
    ESP_LOGE(TAG, "nvs init failed err=0x%x", (unsigned)err);
    esp_rom_printf("wind_net: nvs init failed err=0x%x\n", (unsigned)err);
    return;
  }

  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  (void)esp_netif_create_default_wifi_sta();
  ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_cfg));

  s_wifi_event_group = xEventGroupCreate();
  if(s_wifi_event_group == NULL){
    ESP_LOGE(TAG, "wifi event group create failed");
    esp_rom_printf("wind_net: wifi event group create failed\n");
    return;
  }

  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &s_any_wifi_instance));
  ESP_ERROR_CHECK(esp_event_handler_instance_register(
      IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &s_got_ip_instance));

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
  ESP_ERROR_CHECK(esp_wifi_start());

  bits = xEventGroupWaitBits(
      s_wifi_event_group,
      WIFI_CONNECTED_BIT,
      pdFALSE,
      pdTRUE,
      pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

  if((bits & WIFI_CONNECTED_BIT) == 0){
    ESP_LOGW(TAG, "wifi connect timed out, TCP console disabled");
    esp_rom_printf("wind_net: wifi connect timed out, tcp console disabled\n");
    return;
  }

  net_log_boot("wifi connected; starting tcp listener");
  xTaskCreate(wind_tcp_server_task, "wind_tcp_console", 4096, NULL, 5, NULL);
}

int
wind_net_getc_nonblock(void)
{
  uint8_t c;

  if(s_rxq == NULL)
    return -1;

  if(xQueueReceive(s_rxq, &c, 0) != pdTRUE)
    return -1;

  return (int)c;
}

void
wind_net_putc(char c)
{
  int fd = net_get_client_fd();
  ssize_t sent;

  if(fd < 0)
    return;

  sent = send(fd, &c, 1, MSG_DONTWAIT);
  if(sent == 1)
    return;

  if(sent < 0 && (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR))
    return;

  net_close_client();
}

#else

void
wind_net_console_start(void)
{
  esp_rom_printf("wind_net: tcp console disabled (CONFIG_WIND_WIFI_TCP_CONSOLE_ENABLE=n)\n");
}

int
wind_net_getc_nonblock(void)
{
  return -1;
}

void
wind_net_putc(char c)
{
  (void)c;
}

#endif
