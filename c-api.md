'''
## Kafka C library
> https://github.com/confluentinc/librdkafka/
librdkafka is a C library implementation of the Apache Kafka protocol, providing Producer, Consumer and Admin clients. It was designed with message delivery reliability and high performance in mind, current figures exceed 1 million msgs/second for the producer and 3 million msgs/second for the consumer.
librdkafka is licensed under the 2-clause BSD license.

## getting library & header file 
- 소스(https://github.com/confluentinc/librdkafka/)로 빌드해도 되겠지만, 편의상 yum으로 설치함.
$ yum install librdkafka-devel

## 간단 샘플 만들기
> Reference : https://developer.confluent.io/get-started/c/
- producer와 consumer 두가지에 대한 예제이지만, 여기서는 producer 만 만들어 봅니다.
- Altibase 가 consumer로 사용되는것은, 아래 인시던트에서 만들어본, JDBC sink connector 를 사용하면 될것 같습니다.
  *  INC-47994  [OJT] Kafka Confluent JDBC Connector with Altibase
- https://github.com/confluentinc/librdkafka/tree/master/examples
  * 이것을 이용해도 될것 같긴한데, 이건 설정화일 방식이 아니어서, 참고로 보기만 함.

> Makefile
CC = gcc
CFLAGS = -Wall $(shell pkg-config --cflags glib-2.0 rdkafka)
LDLIBS = $(shell pkg-config --libs glib-2.0 rdkafka)

all: producer

producer: common.c producer.c
	$(CC) $(CFLAGS) $@.c -o $@ $(LDLIBS)

#.c.o :
#	$(CC) $(CFLAGS) -c $*.c

clean :
	rm -f producer


> config.ini

[default]
bootstrap.servers=localhost:9092


> common.c

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


> producer.c

#include <glib.h>
#include <librdkafka/rdkafka.h>

#include "common.c"

#define ARR_SIZE(arr) ( sizeof((arr)) / sizeof((arr[0])) )

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

    // Produce data by selecting random values from these lists.
    int message_count = 10;
    const char *topic = "purchases";
    const char *user_ids[6] = {"eabara", "jsmith", "sgarcia", "jbernard", "htanaka", "awalther"};
    const char *products[5] = {"book", "alarm clock", "t-shirts", "gift card", "batteries"};

    for (int i = 0; i < message_count; i++) {
        const char *key =  user_ids[random() % ARR_SIZE(user_ids)];
        const char *value =  products[random() % ARR_SIZE(products)];
        size_t key_len = strlen(key);
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
            g_message("Produced event to topic %s: key = %12s value = %12s", topic, key, value);
        }

        rd_kafka_poll(producer, 0);
    }

    // Block until the messages are all sent.
    g_message("Flushing final messages..");
    rd_kafka_flush(producer, 10 * 1000);

    if (rd_kafka_outq_len(producer) > 0) {
        g_error("%d message(s) were not delivered", rd_kafka_outq_len(producer));
    }

    g_message("%d events were produced to topic %s.", message_count, topic);

    rd_kafka_destroy(producer);

    return 0;
}


> kafka setup
- 참고:  INC-47994  [OJT] Kafka Confluent JDBC Connector with Altibase
- confluent platform이 설치된 상태에서, confluent service를 시작합니다.
$ confluent local services start


> 예제에서 사용되는 topic인  purchases 를 만듭니다.
$ kafka-topics --create --topic  purchases   --bootstrap-server localhost:9092

> show topic list
$ kafka-topics --list --bootstrap-server localhost:9092

> run producer
$ producer config.ini
** Message: 13:58:52.943: Produced event to topic purchases: key =      sgarcia value =  alarm clock
** Message: 13:58:52.943: Produced event to topic purchases: key =      htanaka value =     t-shirts
** Message: 13:58:52.943: Produced event to topic purchases: key =       jsmith value =    gift card
** Message: 13:58:52.943: Produced event to topic purchases: key =     jbernard value =  alarm clock
** Message: 13:58:52.943: Produced event to topic purchases: key =      sgarcia value =    gift card
** Message: 13:58:52.943: Produced event to topic purchases: key =       eabara value =    batteries
** Message: 13:58:52.943: Produced event to topic purchases: key =       eabara value =    gift card
** Message: 13:58:52.943: Produced event to topic purchases: key =     jbernard value =     t-shirts
** Message: 13:58:52.943: Produced event to topic purchases: key =       jsmith value =  alarm clock
** Message: 13:58:52.943: Produced event to topic purchases: key =     jbernard value =     t-shirts
** Message: 13:58:52.943: Flushing final messages..
** Message: 13:58:52.949: 10 events were produced to topic purchases.


> run console-consumer
$ kafka-console-consumer --topic purchases --bootstrap-server localhost:9092 --from-beginning

'''
