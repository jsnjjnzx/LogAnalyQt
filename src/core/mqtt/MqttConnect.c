/* Pinned Mosquitto adapter: its public v5 connect API is blocking. */
#include "mosquitto_internal.h"
#include "property_common.h"
#include "tls_mosq.h"
#include <mosquitto.h>

int loganalyqt_mqtt_connect_async_v5(struct mosquitto *client, const char *host, const char *address,
        int port, int keepalive, const mosquitto_property *properties)
{
    mosquitto_property_free_all(&client->connect_properties);
    if (properties) {
        int rc = mosquitto_property_check_all(CMD_CONNECT, properties);
        if (rc) return rc;
        rc = mosquitto_property_copy_all(&client->connect_properties, properties);
        if (rc) return rc;
        client->connect_properties->client_generated = true;
    }
    int rc = mosquitto_connect_async(client, address, port, keepalive);
    if (rc > 0) return rc;
#ifdef WITH_TLS
    if (client->ssl) {
        /* DNS is resolved asynchronously by Qt; authenticate the original host. */
        X509_VERIFY_PARAM *param = SSL_get0_param(client->ssl);
        X509_VERIFY_PARAM_set1_ip(param, NULL, 0);
        X509_VERIFY_PARAM_set1_host(param, NULL, 0);
        if (SSL_set_tlsext_host_name(client->ssl, host) != 1) return MOSQ_ERR_TLS;
        if (tls__set_verify_hostname(client, host)) return MOSQ_ERR_TLS;
    }
#endif
    return rc;
}
