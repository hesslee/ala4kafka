#include <glib.h>
#include <librdkafka/rdkafka.h>

#include "cJSON.h"

#define ARR_SIZE(arr) ( sizeof((arr)) / sizeof((arr[0])) )

static void load_config_group(rd_kafka_conf_t *conf,
                              GKeyFile *key_file,
                              const char *group
                              ) {
    char errstr[512];
    g_autoptr(GError) error = NULL;

    gchar **ptr = g_key_file_get_keys(key_file, group, NULL, &error);
    if (error) {
        g_error("%s", error->message);
        exit(1);
    }

    while (*ptr) {
        const char *key = *ptr;
        g_autofree gchar *value = g_key_file_get_string(key_file, group, key, &error);

        if (error) {
            g_error("Reading key: %s", error->message);
            exit(1);
        }

        if (rd_kafka_conf_set(conf, key, value, errstr, sizeof(errstr))
            != RD_KAFKA_CONF_OK
            ) {
            g_error("%s", errstr);
            exit(1);
        }

        ptr++;
    }
}

/* Optional per-message delivery callback (triggered by poll() or flush())
 * when a message has been successfully delivered or permanently
 * failed delivery (after retries).
 */
static void dr_msg_cb (rd_kafka_t *kafka_handle,
                       const rd_kafka_message_t *rkmessage,
                       void *opaque) {
    if (rkmessage->err) {
        g_error("Message delivery failed: %s", rd_kafka_err2str(rkmessage->err));
    }
}

int main (int argc, char **argv) {
    rd_kafka_t *producer;
    rd_kafka_conf_t *conf;
    char errstr[512];

    // Parse the command line.
    if (argc != 2) {
        g_error("Usage: %s <config.ini>", argv[0]);
        return 1;
    }

    // Parse the configuration.
    // See https://github.com/confluentinc/librdkafka/blob/master/CONFIGURATION.md
    const char *config_file = argv[1];

    g_autoptr(GError) error = NULL;
    g_autoptr(GKeyFile) key_file = g_key_file_new();
    if (!g_key_file_load_from_file (key_file, config_file, G_KEY_FILE_NONE, &error)) {
        g_error ("Error loading config file: %s", error->message);
        return 1;
    }

    // Load the relevant configuration sections.
    conf = rd_kafka_conf_new();
    load_config_group(conf, key_file, "default");

    // Install a delivery-error callback.
    rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb);

    // Create the Producer instance.
    producer = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
    if (!producer) {
        g_error("Failed to create new producer: %s", errstr);
        return 1;
    }

    // Configuration object is now owned, and freed, by the rd_kafka_t instance.
    conf = NULL;

    const char *topic = "PURCHASES";

    cJSON *rootK = cJSON_CreateObject();

    // Create the schema object
    cJSON *schemaK = cJSON_CreateObject();
    cJSON_AddStringToObject(schemaK, "type", "struct");
    cJSON_AddBoolToObject(schemaK, "optional", cJSON_False);
    cJSON_AddStringToObject(schemaK, "name", "key");

    // Create the fields array
    cJSON *fieldsK = cJSON_CreateArray();

    // Create the field objects and add them to the fields array
    cJSON *c1K = cJSON_CreateObject();
    cJSON_AddStringToObject(c1K, "type", "int32");
    cJSON_AddBoolToObject(c1K, "optional", cJSON_False);
    cJSON_AddStringToObject(c1K, "field", "C1");
    cJSON_AddItemToArray(fieldsK, c1K);

    // Add the fields array to the schema object
    cJSON_AddItemToObject(schemaK, "fields", fieldsK);

    // Add the schema object to the root object
    cJSON_AddItemToObject(rootK, "schema", schemaK);

    // Create the payload object
    cJSON *payloadK = cJSON_CreateObject();
    cJSON_AddNumberToObject(payloadK, "C1", 42);

    // Add the payload object to the root object
    cJSON_AddItemToObject(rootK, "payload", payloadK);

    char *key = cJSON_PrintUnformatted(rootK);
    size_t key_len = strlen(key);

    cJSON *root = cJSON_CreateObject();

    // Create the schema object
    cJSON *schema = cJSON_CreateObject();
    cJSON_AddStringToObject(schema, "type", "struct");
    cJSON_AddBoolToObject(schema, "optional", cJSON_False);
    cJSON_AddStringToObject(schema, "name", "value");

    // Create the fields array
    cJSON *fields = cJSON_CreateArray();

    // Create the field objects and add them to the fields array
    /*
    cJSON *c1 = cJSON_CreateObject();
    cJSON_AddStringToObject(c1, "type", "int32");
    cJSON_AddBoolToObject(c1, "optional", cJSON_False);
    cJSON_AddStringToObject(c1, "field", "C1");
    cJSON_AddItemToArray(fields, c1);
    */
    cJSON *c2 = cJSON_CreateObject();
    cJSON_AddStringToObject(c2, "type", "string");
    cJSON_AddBoolToObject(c2, "optional", cJSON_False);
    cJSON_AddStringToObject(c2, "field", "C2");
    cJSON_AddItemToArray(fields, c2);

    cJSON *c3 = cJSON_CreateObject();
    cJSON_AddStringToObject(c3, "type", "int32");
    cJSON_AddBoolToObject(c3, "optional", cJSON_False);
    cJSON_AddStringToObject(c3, "field", "C3");
    cJSON_AddItemToArray(fields, c3);

    // Add the fields array to the schema object
    cJSON_AddItemToObject(schema, "fields", fields);

    // Add the schema object to the root object
    cJSON_AddItemToObject(root, "schema", schema);

    // Create the payload object
    cJSON *payload = cJSON_CreateObject();
    //cJSON_AddNumberToObject(payload, "C1", 42);
    cJSON_AddStringToObject(payload, "C2", "Hello, world!");
    cJSON_AddNumberToObject(payload, "C3", 123);

    // Add the payload object to the root object
    cJSON_AddItemToObject(root, "payload", payload);

    char *value = cJSON_PrintUnformatted(root);
    size_t value_len = strlen(value);


        rd_kafka_resp_err_t err;

        err = rd_kafka_producev(producer,
                                RD_KAFKA_V_TOPIC(topic),
                                RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY),
                                RD_KAFKA_V_KEY((void*)key, key_len),
                                RD_KAFKA_V_VALUE((void*)value, value_len),
                                RD_KAFKA_V_OPAQUE(NULL),
                                RD_KAFKA_V_END);

        if (err) {
            g_error("Failed to produce to topic %s: %s", topic, rd_kafka_err2str(err));
            return 1;
        } else {
            g_message("Produced event to topic %s: key = %s value = %s", topic, key, value);
        }

        rd_kafka_poll(producer, 0);

    // Block until the messages are all sent.
    g_message("Flushing final messages..");
    rd_kafka_flush(producer, 10 * 1000);

    if (rd_kafka_outq_len(producer) > 0) {
        g_error("%d message(s) were not delivered", rd_kafka_outq_len(producer));
    }

    rd_kafka_destroy(producer);

    return 0;
}
