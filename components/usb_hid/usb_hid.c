#include "usb_hid.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_private/usb_phy.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "tusb.h"
#include "class/hid/hid_device.h"
#include "class/net/net_device.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include "lwip/pbuf.h"
#include "lwip/ip4_addr.h"
#include "netif/ethernet.h"
#include <string.h>
#include <arpa/inet.h>

static const char *TAG = "usb_hid";

// ============================================================
// Keyboard configuration
// ============================================================

#define HID_REPORT_ID_KB  1

static const hid_locale_t *s_locale = &hid_locale_de;
static volatile bool s_mounted = false;
static usb_phy_handle_t s_phy_hdl = NULL;

typedef struct { uint8_t modifier; uint8_t keys[6]; } kb_report_t;
#define KB_QUEUE_LEN 32
static QueueHandle_t     s_kb_queue = NULL;
static SemaphoreHandle_t s_kb_sent  = NULL;

// ============================================================
// RNDIS / USB Ethernet configuration
// ============================================================

// MAC address of the USB device side (announced to the host via USB descriptors).
// The host side uses this MAC XOR'd LSB.
// Must be globally accessible – TinyUSB reads it for ECM string descriptors.
uint8_t tud_network_mac_address[6] = {0x02, 0x02, 0x84, 0x6A, 0x96, 0x00};

// ESP32 IP on the USB network
#define USB_NET_ESP_IP_A   192
#define USB_NET_ESP_IP_B   168
#define USB_NET_ESP_IP_C     7
#define USB_NET_ESP_IP_D     1

// Host (victim) IP on the USB network
#define USB_NET_HOST_IP_D    2

static struct netif    s_usb_netif;
static bool            s_netif_added    = false;
static volatile bool   s_net_connected  = false;  // true after DHCP ACK sent
static volatile bool   s_rndis_enabled  = false;  // true after ATTACKMODE HID RNDIS

// TX queue: lwIP (tcpip_task) → TinyUSB task
// Queue contains pool indices (uint8_t) only – avoids copying 1.5 KB per item onto stack
#define NET_TX_BUF_SIZE  1520
#define NET_TX_POOL_SIZE    4

typedef struct {
    uint8_t  data[NET_TX_BUF_SIZE];
    uint16_t len;
} net_tx_buf_t;

static net_tx_buf_t      s_net_tx_pool[NET_TX_POOL_SIZE];
static volatile bool     s_net_tx_free[NET_TX_POOL_SIZE];
static QueueHandle_t     s_net_tx_queue   = NULL;   // queue of uint8_t pool indices
static SemaphoreHandle_t s_net_pool_mutex = NULL;

// ============================================================
// USB Descriptors
// ============================================================

static const uint8_t s_hid_report_desc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_REPORT_ID_KB))
};

// Composite device: RNDIS + HID  →  IAD device class (0xEF/0x02/0x01)
static const tusb_desc_device_t s_device_desc = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = TUSB_CLASS_MISC,          // 0xEF
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,      // 0x02
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,         // 0x01
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = 0x046D,
    .idProduct          = 0xC31C,
    .bcdDevice          = 0x0200,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1,
};

// String index 4 = MAC address hex string for ECM (12 ASCII hex chars, uppercase)
// Must match tud_network_mac_address[]
static const char *s_string_desc[] = {
    (char[]){0x09, 0x04},   // 0: Language = English (US)
    "Logitech",             // 1: Manufacturer
    "USB Keyboard",         // 2: Product
    "0000000001",           // 3: Serial number
    "0202846A9600",         // 4: MAC address (for CDC-ECM; 12 uppercase hex chars)
    NULL
};

// Interface numbering
enum {
    ITF_NUM_CDC_COMM = 0,   // RNDIS control interface
    ITF_NUM_CDC_DATA = 1,   // RNDIS data interface
    ITF_NUM_HID      = 2,   // HID keyboard
    ITF_NUM_TOTAL
};

// Endpoint numbers
#define EPNUM_NET_NOTIF  0x81   // EP1 IN  – RNDIS interrupt (notification)
#define EPNUM_NET_OUT    0x02   // EP2 OUT – RNDIS bulk data
#define EPNUM_NET_IN     0x82   // EP2 IN  – RNDIS bulk data
#define EPNUM_HID        0x83   // EP3 IN  – HID keyboard
#define HID_POLL_MS      10

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_RNDIS_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t s_config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    // RNDIS: 2 interfaces (control + data), notification EP1 IN, bulk EP2 OUT+IN
    TUD_RNDIS_DESCRIPTOR(ITF_NUM_CDC_COMM, 0,
                         EPNUM_NET_NOTIF, 8,
                         EPNUM_NET_OUT, EPNUM_NET_IN, CFG_TUD_NET_ENDPOINT_SIZE),
    // HID keyboard on EP3 IN
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(s_hid_report_desc), EPNUM_HID,
                       CFG_TUD_HID_EP_BUFSIZE, HID_POLL_MS),
};

// ============================================================
// TinyUSB Descriptor Callbacks
// ============================================================

uint8_t const *tud_descriptor_device_cb(void)
{
    return (uint8_t const *)&s_device_desc;
}

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    return s_config_desc;
}

static uint16_t s_str_buf[32];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;
    uint8_t chr_count;

    if (index == 0) {
        s_str_buf[1] = ((uint16_t)s_string_desc[0][1] << 8) | s_string_desc[0][0];
        chr_count = 1;
    } else {
        size_t table_size = (sizeof(s_string_desc) / sizeof(s_string_desc[0])) - 1;
        if (index >= table_size) return NULL;
        const char *str = s_string_desc[index];
        chr_count = (uint8_t)strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) s_str_buf[1 + i] = str[i];
    }

    s_str_buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return s_str_buf;
}

// ============================================================
// TinyUSB Connection Callbacks
// ============================================================

void tud_mount_cb(void)
{
    ESP_LOGI(TAG, "USB: host mounted");
    s_mounted = true;
}

void tud_umount_cb(void)
{
    ESP_LOGI(TAG, "USB: host unmounted");
    s_mounted = false;
}

void tud_suspend_cb(bool remote_wakeup_en)
{
    (void)remote_wakeup_en;
    ESP_LOGD(TAG, "USB: suspended");
}

void tud_resume_cb(void)
{
    ESP_LOGD(TAG, "USB: resumed");
    s_mounted = true;
}

// ============================================================
// TinyUSB HID Callbacks
// ============================================================

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_hid_report_desc;
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const *report, uint16_t len)
{
    (void)instance; (void)report; (void)len;
    if (s_kb_sent) xSemaphoreGive(s_kb_sent);
}

// Forward declarations (definitions follow in lwIP section below)
static void usb_netif_add_in_tcpip(void *ctx);

// ============================================================
// DHCP Interception (raw Ethernet frame approach)
// Runs in TinyUSB task context – tud_network_xmit() safe to call here.
// This avoids any conflict with the WiFi AP DHCP server on port 67.
// ============================================================

#pragma pack(push, 1)
typedef struct {
    uint8_t  dst[6];
    uint8_t  src[6];
    uint16_t etype;       // big-endian 0x0800 = IPv4
} eth_hdr_t;

typedef struct {
    uint8_t  ver_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_len;   // big-endian
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  proto;       // 0x11 = UDP
    uint16_t checksum;
    uint32_t src_ip;
    uint32_t dst_ip;
} ip4_hdr_t;

typedef struct {
    uint16_t src_port;    // big-endian
    uint16_t dst_port;    // big-endian
    uint16_t length;      // big-endian
    uint16_t checksum;
} udp_hdr_t;

typedef struct {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  options[64]; // variable, we send at most 64 bytes of options
} dhcp_pkt_t;
#pragma pack(pop)

#define DHCP_MAGIC        0x63825363UL
#define DHCP_DISCOVER     1
#define DHCP_OFFER        2
#define DHCP_REQUEST      3
#define DHCP_ACK          5
#define OPT_MSG_TYPE      53
#define OPT_SERVER_ID     54
#define OPT_LEASE_TIME    51
#define OPT_SUBNET_MASK    1
#define OPT_ROUTER         3
#define OPT_DNS            6
#define OPT_END          255

// Build and transmit a DHCP OFFER or ACK in response to a received DHCP frame.
// caller_frame: full Ethernet frame from host (DISCOVER or REQUEST)
// msg_type: DHCP_OFFER or DHCP_ACK
static void dhcp_send_reply(const uint8_t *caller_frame, uint16_t frame_len, uint8_t msg_type)
{
    static uint8_t tx_frame[sizeof(eth_hdr_t) + sizeof(ip4_hdr_t) + sizeof(udp_hdr_t) + sizeof(dhcp_pkt_t)];
    const eth_hdr_t  *req_eth  = (const eth_hdr_t *)caller_frame;
    const ip4_hdr_t  *req_ip   = (const ip4_hdr_t *)(caller_frame + sizeof(eth_hdr_t));
    const udp_hdr_t  *req_udp  = (const udp_hdr_t *)((const uint8_t *)req_ip + ((req_ip->ver_ihl & 0x0F) * 4));
    const dhcp_pkt_t *req_dhcp = (const dhcp_pkt_t *)((const uint8_t *)req_udp + sizeof(udp_hdr_t));

    (void)frame_len;

    memset(tx_frame, 0, sizeof(tx_frame));

    // — Ethernet header —
    eth_hdr_t *eth = (eth_hdr_t *)tx_frame;
    memcpy(eth->dst, req_eth->src, 6);   // unicast to requester
    memcpy(eth->src, tud_network_mac_address, 6);
    eth->etype = htons(0x0800);

    // — IP header —
    ip4_hdr_t *ip = (ip4_hdr_t *)(tx_frame + sizeof(eth_hdr_t));
    uint16_t dhcp_payload_len = (uint16_t)(sizeof(dhcp_pkt_t));
    uint16_t udp_total = (uint16_t)(sizeof(udp_hdr_t) + dhcp_payload_len);
    uint16_t ip_total  = (uint16_t)(sizeof(ip4_hdr_t) + udp_total);

    ip->ver_ihl   = 0x45;
    ip->total_len = htons(ip_total);
    ip->ttl       = 128;
    ip->proto     = 0x11;
    ip->src_ip    = htonl((USB_NET_ESP_IP_A << 24) | (USB_NET_ESP_IP_B << 16) |
                          (USB_NET_ESP_IP_C <<  8) |  USB_NET_ESP_IP_D);
    ip->dst_ip    = htonl(0xFFFFFFFFUL);  // broadcast

    // IPv4 checksum
    {
        uint32_t sum = 0;
        const uint16_t *p = (const uint16_t *)ip;
        for (int i = 0; i < (int)sizeof(ip4_hdr_t) / 2; i++) sum += ntohs(p[i]);
        while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
        ip->checksum = htons((uint16_t)(~sum));
    }

    // — UDP header —
    udp_hdr_t *udp = (udp_hdr_t *)((uint8_t *)ip + sizeof(ip4_hdr_t));
    udp->src_port = htons(67);
    udp->dst_port = htons(68);
    udp->length   = htons(udp_total);
    // UDP checksum left 0 (optional for IPv4)

    // — DHCP payload —
    dhcp_pkt_t *dhcp = (dhcp_pkt_t *)((uint8_t *)udp + sizeof(udp_hdr_t));
    dhcp->op     = 2;   // BOOTREPLY
    dhcp->htype  = 1;
    dhcp->hlen   = 6;
    dhcp->xid    = req_dhcp->xid;
    dhcp->flags  = req_dhcp->flags;
    dhcp->yiaddr = htonl((USB_NET_ESP_IP_A << 24) | (USB_NET_ESP_IP_B << 16) |
                         (USB_NET_ESP_IP_C <<  8) |  USB_NET_HOST_IP_D);
    dhcp->siaddr = htonl((USB_NET_ESP_IP_A << 24) | (USB_NET_ESP_IP_B << 16) |
                         (USB_NET_ESP_IP_C <<  8) |  USB_NET_ESP_IP_D);
    memcpy(dhcp->chaddr, req_dhcp->chaddr, 16);
    dhcp->magic = htonl(DHCP_MAGIC);

    // DHCP options
    uint8_t *opt = dhcp->options;
    uint32_t server_ip = ntohl(dhcp->siaddr);
    uint32_t lease     = 86400UL;
    uint32_t mask      = (255<<24)|(255<<16)|(255<<8)|0;
    uint32_t router    = ntohl(dhcp->siaddr);

    *opt++ = OPT_MSG_TYPE; *opt++ = 1; *opt++ = msg_type;

    *opt++ = OPT_SERVER_ID;   *opt++ = 4;
    *opt++ = (server_ip >> 24) & 0xFF; *opt++ = (server_ip >> 16) & 0xFF;
    *opt++ = (server_ip >>  8) & 0xFF; *opt++ = (server_ip      ) & 0xFF;

    *opt++ = OPT_LEASE_TIME; *opt++ = 4;
    *opt++ = (lease >> 24) & 0xFF; *opt++ = (lease >> 16) & 0xFF;
    *opt++ = (lease >>  8) & 0xFF; *opt++ = (lease      ) & 0xFF;

    *opt++ = OPT_SUBNET_MASK; *opt++ = 4;
    *opt++ = (mask >> 24) & 0xFF; *opt++ = (mask >> 16) & 0xFF;
    *opt++ = (mask >>  8) & 0xFF; *opt++ = (mask      ) & 0xFF;

    *opt++ = OPT_ROUTER; *opt++ = 4;
    *opt++ = (router >> 24) & 0xFF; *opt++ = (router >> 16) & 0xFF;
    *opt++ = (router >>  8) & 0xFF; *opt++ = (router      ) & 0xFF;

    *opt++ = OPT_DNS; *opt++ = 4;
    *opt++ = (router >> 24) & 0xFF; *opt++ = (router >> 16) & 0xFF;
    *opt++ = (router >>  8) & 0xFF; *opt++ = (router      ) & 0xFF;

    *opt++ = OPT_END;

    uint16_t tx_len = (uint16_t)(sizeof(eth_hdr_t) + ip_total);

    if (tud_network_can_xmit(tx_len)) {
        tud_network_xmit(tx_frame, tx_len);
        ESP_LOGD(TAG, "RNDIS DHCP %s sent", msg_type == DHCP_OFFER ? "OFFER" : "ACK");
    } else {
        ESP_LOGW(TAG, "RNDIS DHCP reply dropped – TX not ready");
    }
}

// ============================================================
// TinyUSB Network Callbacks  (called from TinyUSB task)
// ============================================================

// Called by TinyUSB when a complete Ethernet frame arrives from the host.
bool tud_network_recv_cb(const uint8_t *src, uint16_t size)
{
    // DHCP is always processed so the host gets an IP as soon as the device connects.
    // The lwIP netif (and thus the HTTP server) is only registered after ATTACKMODE HID RNDIS.
    // Non-DHCP frames are dropped until the netif is registered (s_netif_added).
    if (size < (uint16_t)(sizeof(eth_hdr_t) + sizeof(ip4_hdr_t) + sizeof(udp_hdr_t))) {
        tud_network_recv_renew();
        return true;
    }

    const eth_hdr_t *eth = (const eth_hdr_t *)src;
    if (ntohs(eth->etype) == 0x0800) {
        const ip4_hdr_t *ip = (const ip4_hdr_t *)(src + sizeof(eth_hdr_t));
        if (ip->proto == 0x11) {  // UDP
            int ihl = (ip->ver_ihl & 0x0F) * 4;
            const udp_hdr_t *udp = (const udp_hdr_t *)((const uint8_t *)ip + ihl);
            if (ntohs(udp->dst_port) == 67) {
                // DHCP packet – handle inline, do NOT forward to lwIP
                const dhcp_pkt_t *dhcp = (const dhcp_pkt_t *)((const uint8_t *)udp + sizeof(udp_hdr_t));
                if (ntohl(dhcp->magic) == DHCP_MAGIC) {
                    // Find message type option (53)
                    uint8_t dhcp_type = 0;
                    const uint8_t *opt = dhcp->options;
                    while (*opt != OPT_END && opt < (const uint8_t *)dhcp + sizeof(dhcp_pkt_t)) {
                        if (*opt == OPT_MSG_TYPE && *(opt+1) == 1) { dhcp_type = *(opt+2); break; }
                        opt += 2 + *(opt+1);
                    }

                    if (dhcp_type == DHCP_DISCOVER) {
                        ESP_LOGI(TAG, "RNDIS DHCP DISCOVER received");
                        dhcp_send_reply(src, size, DHCP_OFFER);
                    } else if (dhcp_type == DHCP_REQUEST) {
                        ESP_LOGI(TAG, "RNDIS DHCP REQUEST received");
                        dhcp_send_reply(src, size, DHCP_ACK);
                        s_net_connected = true;
                        ESP_LOGI(TAG, "RNDIS: host IP assigned 192.168.7.%d – network ready",
                                 USB_NET_HOST_IP_D);
                    }
                }
                tud_network_recv_renew();
                return true;
            }
        }
    }

    // Forward non-DHCP frames to lwIP
    if (s_netif_added) {
        struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
        if (p) {
            pbuf_take(p, src, size);
            if (s_usb_netif.input(p, &s_usb_netif) != ERR_OK) {
                pbuf_free(p);
            }
        }
    }

    tud_network_recv_renew();
    return true;
}

// Called by TinyUSB to fill its TX buffer.
// ref = pointer to our TX buffer, arg = length (as set in tud_network_xmit call)
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg)
{
    memcpy(dst, ref, arg);
    return arg;
}

// Called by TinyUSB when the RNDIS interface is initialized / reset by the host.
void tud_network_init_cb(void)
{
    ESP_LOGI(TAG, "RNDIS: host initialized network interface");
    // Register lwIP netif only after ATTACKMODE HID RNDIS has been called.
    // This keeps the device functionally HID-only until the script requests RNDIS.
    if (s_rndis_enabled && !s_netif_added) {
        tcpip_callback(usb_netif_add_in_tcpip, NULL);
    }
}

// ============================================================
// lwIP netif integration
// ============================================================

// linkoutput_fn runs in tcpip_task context – must NOT call tud_network_xmit directly.
// Instead, copy pbuf to a pool buffer and enqueue for TinyUSB task.
static err_t usb_netif_linkoutput(struct netif *netif, struct pbuf *p)
{
    (void)netif;

    if (!tud_ready() || p->tot_len > NET_TX_BUF_SIZE) return ERR_USE;

    // Find a free TX pool buffer
    int idx = -1;
    if (xSemaphoreTake(s_net_pool_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
        for (int i = 0; i < NET_TX_POOL_SIZE; i++) {
            if (s_net_tx_free[i]) { idx = i; s_net_tx_free[i] = false; break; }
        }
        xSemaphoreGive(s_net_pool_mutex);
    }

    if (idx < 0) {
        ESP_LOGW(TAG, "RNDIS TX pool full, dropping packet");
        return ERR_MEM;
    }

    s_net_tx_pool[idx].len = (uint16_t)pbuf_copy_partial(p, s_net_tx_pool[idx].data,
                                                          NET_TX_BUF_SIZE, 0);
    uint8_t idx_byte = (uint8_t)idx;
    if (xQueueSend(s_net_tx_queue, &idx_byte, 0) != pdTRUE) {
        s_net_tx_free[idx] = true;
        return ERR_MEM;
    }

    return ERR_OK;
}

static err_t usb_netif_init(struct netif *netif)
{
    netif->mtu        = CFG_TUD_NET_MTU;
    netif->flags      = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
                        NETIF_FLAG_LINK_UP   | NETIF_FLAG_UP;
    netif->name[0]    = 'U';
    netif->name[1]    = 'S';
    netif->linkoutput = usb_netif_linkoutput;
    netif->output     = etharp_output;
    netif->hwaddr_len = 6;
    // Use local MAC XOR LSB (must differ from device MAC)
    memcpy(netif->hwaddr, tud_network_mac_address, 6);
    netif->hwaddr[5] ^= 0x01;
    return ERR_OK;
}

// Called from tcpip_task via tcpip_callback() – safe to call lwIP APIs here
static void usb_netif_add_in_tcpip(void *ctx)
{
    (void)ctx;
    if (s_netif_added) return;

    const ip4_addr_t ipaddr  = { htonl(((uint32_t)USB_NET_ESP_IP_A << 24) |
                                       ((uint32_t)USB_NET_ESP_IP_B << 16) |
                                       ((uint32_t)USB_NET_ESP_IP_C <<  8) |
                                        (uint32_t)USB_NET_ESP_IP_D) };
    const ip4_addr_t netmask = { htonl(0xFFFFFF00UL) };
    const ip4_addr_t gateway = { 0 };

    struct netif *result = netif_add(&s_usb_netif, &ipaddr, &netmask, &gateway,
                                     NULL, usb_netif_init, tcpip_input);
    if (result) {
        s_netif_added = true;
        ESP_LOGI(TAG, "RNDIS lwIP netif added: 192.168.%d.%d",
                 USB_NET_ESP_IP_C, USB_NET_ESP_IP_D);
    } else {
        ESP_LOGE(TAG, "RNDIS: netif_add failed");
    }
}

// ============================================================
// TinyUSB Task
// ============================================================

static void tusb_device_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "TinyUSB device task started");
    while (1) {
        tud_task();

        // Drain one pending HID report per iteration
        if (s_kb_queue && tud_hid_ready()) {
            kb_report_t r;
            if (xQueueReceive(s_kb_queue, &r, 0) == pdTRUE) {
                tud_hid_keyboard_report(HID_REPORT_ID_KB, r.modifier, r.keys);
            }
        }

        // Drain one pending network TX per iteration
        if (s_net_tx_queue) {
            uint8_t idx;
            if (xQueueReceive(s_net_tx_queue, &idx, 0) == pdTRUE) {
                if (tud_network_can_xmit(s_net_tx_pool[idx].len)) {
                    tud_network_xmit(s_net_tx_pool[idx].data, s_net_tx_pool[idx].len);
                }
                if (xSemaphoreTake(s_net_pool_mutex, 0) == pdTRUE) {
                    s_net_tx_free[idx] = true;
                    xSemaphoreGive(s_net_pool_mutex);
                }
            }
        }
    }
}

// ============================================================
// Locale API
// ============================================================

void hid_locale_set(const hid_locale_t *loc)
{
    if (loc) s_locale = loc;
}

const hid_locale_t *hid_locale_get(void)
{
    return s_locale;
}

static bool hid_locale_find_ascii(uint8_t c, uint8_t *out_mod, uint8_t *out_key)
{
    if (!s_locale || !s_locale->ascii || c >= s_locale->ascii_len) return false;
    *out_mod = s_locale->ascii[c * 2];
    *out_key = s_locale->ascii[c * 2 + 1];
    return (*out_key != KEY_NONE);
}

static bool hid_locale_find_utf8(const uint8_t seq[4], uint8_t *out_mod, uint8_t *out_key)
{
    if (!s_locale || !s_locale->utf8 || s_locale->utf8_len == 0) return false;
    for (size_t i = 0; i < s_locale->utf8_len; i++) {
        const uint8_t *e = &s_locale->utf8[i * 6];
        if (e[0] == seq[0] && e[1] == seq[1] && e[2] == seq[2] && e[3] == seq[3]) {
            *out_mod = e[4];
            *out_key = e[5];
            return (*out_key != KEY_NONE);
        }
    }
    return false;
}

bool hid_locale_set_by_name(const char *name)
{
    if (strcmp(name, "DE") == 0)   { hid_locale_set(&hid_locale_de);   return true; }
    if (strcmp(name, "US") == 0)   { hid_locale_set(&hid_locale_us);   return true; }
    if (strcmp(name, "GB") == 0)   { hid_locale_set(&hid_locale_gb);   return true; }
    if (strcmp(name, "FR") == 0)   { hid_locale_set(&hid_locale_fr);   return true; }
    if (strcmp(name, "ES") == 0)   { hid_locale_set(&hid_locale_es);   return true; }
    if (strcmp(name, "IT") == 0)   { hid_locale_set(&hid_locale_it);   return true; }
    if (strcmp(name, "PT") == 0)   { hid_locale_set(&hid_locale_pt);   return true; }
    if (strcmp(name, "DK") == 0)   { hid_locale_set(&hid_locale_dk);   return true; }
    if (strcmp(name, "BE") == 0)   { hid_locale_set(&hid_locale_be);   return true; }
    if (strcmp(name, "RU") == 0)   { hid_locale_set(&hid_locale_ru);   return true; }
    if (strcmp(name, "SI") == 0)   { hid_locale_set(&hid_locale_si);   return true; }
    if (strcmp(name, "SK") == 0)   { hid_locale_set(&hid_locale_sk);   return true; }
    if (strcmp(name, "CZ") == 0)   { hid_locale_set(&hid_locale_cz);   return true; }
    if (strcmp(name, "HU") == 0)   { hid_locale_set(&hid_locale_hu);   return true; }
    if (strcmp(name, "CAFR") == 0) { hid_locale_set(&hid_locale_cafr); return true; }
    if (strcmp(name, "CHDE") == 0) { hid_locale_set(&hid_locale_chde); return true; }
    if (strcmp(name, "CHFR") == 0) { hid_locale_set(&hid_locale_chfr); return true; }
    if (strcmp(name, "BG") == 0)   { hid_locale_set(&hid_locale_bg);   return true; }
    return false;
}

// ============================================================
// Public API
// ============================================================

esp_err_t usb_hid_init(void)
{
    ESP_LOGI(TAG, "Initializing USB HID+RNDIS composite device");

    // HID queue + semaphore
    s_kb_queue = xQueueCreate(KB_QUEUE_LEN, sizeof(kb_report_t));
    s_kb_sent  = xSemaphoreCreateBinary();
    if (!s_kb_queue || !s_kb_sent) {
        ESP_LOGE(TAG, "Failed to create HID queue/semaphore");
        return ESP_ERR_NO_MEM;
    }

    // Network TX queue (indices only) + pool mutex
    for (int i = 0; i < NET_TX_POOL_SIZE; i++) s_net_tx_free[i] = true;
    s_net_tx_queue   = xQueueCreate(NET_TX_POOL_SIZE, sizeof(uint8_t));
    s_net_pool_mutex = xSemaphoreCreateMutex();
    if (!s_net_tx_queue || !s_net_pool_mutex) {
        ESP_LOGE(TAG, "Failed to create RNDIS TX queue/mutex");
        return ESP_ERR_NO_MEM;
    }

    // Switch USB MUX to OTG Device mode
    const usb_phy_config_t phy_cfg = {
        .controller  = USB_PHY_CTRL_OTG,
        .target      = USB_PHY_TARGET_INT,
        .otg_mode    = USB_OTG_MODE_DEVICE,
        .otg_speed   = USB_PHY_SPEED_FULL,
    };
    esp_err_t ret = usb_new_phy(&phy_cfg, &s_phy_hdl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "usb_new_phy() failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (!tusb_init()) {
        ESP_LOGE(TAG, "tusb_init() failed");
        return ESP_FAIL;
    }

    // Do NOT register RNDIS netif here – RNDIS is disabled until ATTACKMODE HID RNDIS.
    // Call usb_hid_enable_rndis() from DuckyScript to activate network functionality.

    // TinyUSB task at priority 5 (lwIP tcpip_task usually at 18, HID typing at 1)
    BaseType_t task_ret = xTaskCreate(tusb_device_task, "tusb", 10240, NULL, 5, NULL);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create TinyUSB task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "USB HID ready (RNDIS inactive until ATTACKMODE HID RNDIS)");
    return ESP_OK;
}

esp_err_t usb_hid_enable_rndis(void)
{
    if (s_rndis_enabled) return ESP_OK;  // already active

    ESP_LOGI(TAG, "RNDIS: enabling network interface");
    s_rndis_enabled = true;

    // If the host already initialized RNDIS before we enabled it, register netif now.
    // Otherwise tud_network_init_cb() will do it on the next host init.
    if (!s_netif_added) {
        tcpip_callback(usb_netif_add_in_tcpip, NULL);
    }

    return ESP_OK;
}

bool usb_hid_is_mounted(void)
{
    return s_mounted;
}

void usb_hid_wait_mounted(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    const uint32_t step = 50;
    while (!s_mounted) {
        if (timeout_ms > 0 && elapsed >= timeout_ms) break;
        vTaskDelay(pdMS_TO_TICKS(step));
        elapsed += step;
    }
    if (s_mounted) {
        ESP_LOGI(TAG, "Host connected after %lu ms", (unsigned long)elapsed);
    } else {
        ESP_LOGW(TAG, "wait_mounted: timeout (%lu ms)", (unsigned long)timeout_ms);
    }
}

bool usb_net_is_connected(void)
{
    // Both conditions must be true:
    // 1. Host completed DHCP (has an IP)
    // 2. lwIP netif is registered (HTTP server accessible via RNDIS)
    return s_net_connected && s_netif_added;
}

// ============================================================
// Keyboard API
// ============================================================

static bool hid_queue_report(uint8_t modifiers, const uint8_t *keys)
{
    if (!s_kb_queue || !s_mounted) return false;

    kb_report_t r;
    r.modifier = modifiers;
    if (keys) memcpy(r.keys, keys, 6);
    else      memset(r.keys, 0,   6);

    xSemaphoreTake(s_kb_sent, 0);

    if (xQueueSend(s_kb_queue, &r, pdMS_TO_TICKS(1000)) != pdTRUE) return false;
    if (xSemaphoreTake(s_kb_sent, pdMS_TO_TICKS(2000)) != pdTRUE) return false;

    return true;
}

void usb_hid_keyboard_press(uint8_t keycode, uint8_t modifiers)
{
    uint8_t keys[6] = {keycode, 0, 0, 0, 0, 0};
    hid_queue_report(modifiers, keys);
}

void usb_hid_keyboard_release(void)
{
    hid_queue_report(0, NULL);
}

void usb_hid_keyboard_raw_report(uint8_t modifiers, const uint8_t keys[6])
{
    hid_queue_report(modifiers, keys);
}

void usb_hid_keyboard_type_char(char c)
{
    uint8_t mod = 0, key = 0;
    if (!hid_locale_find_ascii((uint8_t)c, &mod, &key)) return;
    usb_hid_keyboard_press(key, mod);
    vTaskDelay(pdMS_TO_TICKS(10));
    usb_hid_keyboard_release();
    vTaskDelay(pdMS_TO_TICKS(10));
}

void usb_hid_keyboard_type_string(const char *str)
{
    if (!str) return;
    const uint8_t *p = (const uint8_t *)str;
    while (*p) {
        uint8_t b0 = p[0];
        int seq_len;
        if      (b0 < 0x80)            seq_len = 1;
        else if ((b0 & 0xE0) == 0xC0)  seq_len = 2;
        else if ((b0 & 0xF0) == 0xE0)  seq_len = 3;
        else if ((b0 & 0xF8) == 0xF0)  seq_len = 4;
        else { p++; continue; }

        if (seq_len == 1) {
            usb_hid_keyboard_type_char((char)b0);
            p++;
        } else {
            uint8_t seq[4] = {0, 0, 0, 0};
            for (int i = 0; i < seq_len && p[i]; i++) seq[i] = p[i];
            uint8_t mod = 0, key = 0;
            if (hid_locale_find_utf8(seq, &mod, &key)) {
                usb_hid_keyboard_press(key, mod);
                vTaskDelay(pdMS_TO_TICKS(10));
                usb_hid_keyboard_release();
                vTaskDelay(pdMS_TO_TICKS(10));
            }
            p += seq_len;
        }
    }
}

// ============================================================
// Mouse (stubs)
// ============================================================

void usb_hid_mouse_move(int8_t x, int8_t y)   { (void)x; (void)y; }
void usb_hid_mouse_click(uint8_t buttons)      { (void)buttons; }
void usb_hid_mouse_scroll(int8_t amount)       { (void)amount; }
