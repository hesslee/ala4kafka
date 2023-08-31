https://docs.confluent.io/platform/current/connect/devguide.html

CREATE REPLICATION ALA1 FOR ANALYSIS 
WITH '127.0.0.1', 47146
FROM ala.ala_t1 TO ala.ala_t1;
