/******************************************************************************
 * Replication to Altibase DBMS Sample                                        *
 *      based on Committed Transaction Only                                   *
 ******************************************************************************/

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Include Altibase ODBC header */
#include <sqlcli.h>

/* Include Altibase Log Analysis API header */
#include <alaAPI.h>

/* User-specific Definitions */
#define QUERY_SIZE      (4196)          /* SQL Query Buffer Size */
#define ALA_LOG_FILE    "ALA1.log"      /* Log File Name */
#define ALA_NAME        "ALA1"          /* XLog Sender Name */
#define SOCKET_TYPE     "TCP"           /* TCP or UNIX */
#define PEER_IP         "127.0.0.1"     /* TCP : XLog Sender IP */
#define MY_PORT         (47146)         /* TCP : XLog Collector Listen Port */

/* Get XLog from XLog Sender, after handshake with XLog Sender */
ALA_RC runXLogCollector(ALA_Handle, ALA_ErrorMgr *);

/* And, apply XLog to Altibase DBMS */
ALA_RC applyXLogToAltibase(ALA_Handle, ALA_XLog *, ALA_ErrorMgr *);

/* Print error to console */
void   printAlaErr(ALA_ErrorMgr * aErrorMgr);
void mytest(ALA_Table * aTable, ALA_XLog * aXLog);

/* Start function */
int main(void)
{
    ALA_Handle      sHandle;            /* XLog Collector Handle */
    ALA_ErrorMgr    sErrorMgr;          /* Error Manager */
    char            sSocketInfo[128];   /* XLog Sender/Collector Socket Information */
    unsigned int    sStep = 0;

    /**************************************************************************
     * ALA Initialization                                                     *
     **************************************************************************/

    /* Initialize Error Manager */
    (void)ALA_ClearErrorMgr(&sErrorMgr);

    /* Initialize ALA API environment */
    if(ALA_InitializeAPI(ALA_FALSE, &sErrorMgr) != ALA_SUCCESS)
    {
        printAlaErr(&sErrorMgr);
        goto FINALYZE;
    }
    sStep = 4;

    /* Initialize ALA Logging */
    if(ALA_EnableLogging((const signed char *)".",          /* Current Directory */
                         (const signed char *)ALA_LOG_FILE, /* Log File Name */
                         10 * 1024 * 1024,                  /* Log File Size */
                         20,                                /* Maximum Previous Log File Count */
                         &sErrorMgr)
            != ALA_SUCCESS)
    {
        printAlaErr(&sErrorMgr);
        goto FINALYZE;
    }
    sStep = 5;

    /* Create XLogCollector */
    memset(sSocketInfo, 0x00, 128);
    sprintf(sSocketInfo, "SOCKET=%s;PEER_IP=%s;MY_PORT=%d", SOCKET_TYPE, PEER_IP, MY_PORT);
    if(ALA_CreateXLogCollector((const signed char *)ALA_NAME,
                               (const signed char *)sSocketInfo,
                               10000,               /* XLog Pool Size */
                               ALA_TRUE,            /* Use Committed Transaction Buffer */
                               100,                 /* ACK Per XLog Count */
                               &sHandle,
                               &sErrorMgr)
            != ALA_SUCCESS)
    {
        printAlaErr(&sErrorMgr);
        goto FINALYZE;
    }
    sStep = 6;

    /* Set Timeouts */
    if(ALA_SetHandshakeTimeout(sHandle,  600, &sErrorMgr) != ALA_SUCCESS)
    {
        printAlaErr(&sErrorMgr);
        goto FINALYZE;
    }

    if(ALA_SetReceiveXLogTimeout(sHandle, 10, &sErrorMgr) != ALA_SUCCESS)
    {
        printAlaErr(&sErrorMgr);
        goto FINALYZE;
    }

    /**************************************************************************
     * Using XLog Collector                                                   *
     **************************************************************************/

    (void)runXLogCollector(sHandle, &sErrorMgr);

  FINALYZE:
    /**************************************************************************
     * Finalization                                                           *
     **************************************************************************/

    switch(sStep)
    {
        case 6:
            /* Destroy XLog Collector */
            (void)ALA_DestroyXLogCollector(sHandle, &sErrorMgr);

        case 5:
            /* Finalize Logging */
            (void)ALA_DisableLogging(&sErrorMgr);

        case 4:
            /* Destroy ALA API environment */
            (void)ALA_DestroyAPI(ALA_TRUE, &sErrorMgr);

        default:
            break;
    }
    return 0;
}

ALA_RC runXLogCollector(ALA_Handle aHandle, ALA_ErrorMgr * aErrorMgr)
{
    ALA_XLog       * sXLog         = NULL;
    ALA_XLogHeader * sXLogHeader   = NULL;
    UInt             sErrorCode;
    ALA_ErrorLevel   sErrorLevel;
    ALA_BOOL         sReplStopFlag = ALA_FALSE;
    ALA_BOOL         sDummyFlag    = ALA_FALSE;
    ALA_BOOL         sAckFlag;

    /* Run until ALA_ERROR_FATAL Error occurs or REPL_STOP XLog arrives */
    while(sReplStopFlag != ALA_TRUE)
    {
        /* Wait and Handshake with XLog Sender */
        if(ALA_Handshake(aHandle, aErrorMgr) != ALA_SUCCESS)
        {
            printAlaErr(aErrorMgr);
            (void)ALA_GetErrorLevel(aErrorMgr, &sErrorLevel);
            if(sErrorLevel == ALA_ERROR_FATAL)
            {
                return ALA_FAILURE;
            }
            /* Wait and Handshake with XLog Sender */
            continue;
        }

        while(sReplStopFlag != ALA_TRUE)
        {
            /* Get XLog from XLog Queue */
            if(ALA_GetXLog(aHandle, (const ALA_XLog **)&sXLog, aErrorMgr) != ALA_SUCCESS)
            {
                printAlaErr(aErrorMgr);
                (void)ALA_GetErrorLevel(aErrorMgr, &sErrorLevel);
                if(sErrorLevel == ALA_ERROR_FATAL)
                {
                    return ALA_FAILURE;
                }
                /* Wait and Handshake with XLog Sender */
                break;
            }
            else
            {
                /* If XLog is NULL, then Receive XLog */
                if(sXLog == NULL)
                {
                    /* Receive XLog and Insert into Queue */
                    if(ALA_ReceiveXLog(aHandle, &sDummyFlag, aErrorMgr) != ALA_SUCCESS)
                    {
                        printAlaErr(aErrorMgr);
                        (void)ALA_GetErrorLevel(aErrorMgr, &sErrorLevel);
                        if(sErrorLevel == ALA_ERROR_FATAL)
                        {
                            return ALA_FAILURE;
                        }
                        else
                        {
                            (void)ALA_GetErrorCode(aErrorMgr, &sErrorCode);
                            if(sErrorCode == 0x52014)   /* Timeout */
                            {
                                /* Receive XLog and Insert into Queue */
                                continue;
                            }
                        }
                        /* Wait and Handshake with XLog Sender */
                        break;
                    }

                    /* Get XLog from XLog Queue */
                    continue;
                }

                /* Get XLog Header */
                (void)ALA_GetXLogHeader(sXLog,
                                        (const ALA_XLogHeader **)&sXLogHeader,
                                        aErrorMgr);

                /* Check REPL_STOP XLog */
                if(sXLogHeader->mType == XLOG_TYPE_REPL_STOP)
                {
                    sReplStopFlag = ALA_TRUE;
                }

                /* Apply XLog to Altibase DBMS */
                sAckFlag = ALA_FALSE;
                switch(sXLogHeader->mType)
                {
                    case XLOG_TYPE_COMMIT            :
                    case XLOG_TYPE_ABORT             :  /* Unused in Committed Transaction Only */
                    case XLOG_TYPE_REPL_STOP         :
                        (void)applyXLogToAltibase(aHandle, sXLog, aErrorMgr);
                        sAckFlag = ALA_TRUE;
                        break;

                    case XLOG_TYPE_INSERT            :
                    case XLOG_TYPE_UPDATE            :
                    case XLOG_TYPE_DELETE            :
                    case XLOG_TYPE_SP_SET            :  /* Unused in Committed Transaction Only */
                    case XLOG_TYPE_SP_ABORT          :  /* Unused in Committed Transaction Only */
                        (void)applyXLogToAltibase(aHandle, sXLog, aErrorMgr);
                        break;

                    case XLOG_TYPE_KEEP_ALIVE        :
                        sAckFlag = ALA_TRUE;
                        break;

                    case XLOG_TYPE_LOB_CURSOR_OPEN   :
                    case XLOG_TYPE_LOB_CURSOR_CLOSE  :
                    case XLOG_TYPE_LOB_PREPARE4WRITE :
                    case XLOG_TYPE_LOB_PARTIAL_WRITE :
                    case XLOG_TYPE_LOB_FINISH2WRITE  :
                    case XLOG_TYPE_LOB_TRIM          :
                    default :
                        break;
                }

                /* Free XLog */
                if(ALA_FreeXLog(aHandle, sXLog, aErrorMgr) != ALA_SUCCESS)
                {
                    printAlaErr(aErrorMgr);
                    (void)ALA_GetErrorLevel(aErrorMgr, &sErrorLevel);
                    if(sErrorLevel == ALA_ERROR_FATAL)
                    {
                        return ALA_FAILURE;
                    }
                    /* Wait and Handshake with XLog Sender */
                    break;
                }

                /* Send ACK to XLog Sender */
                if(sAckFlag != ALA_FALSE)
                {
                    if(ALA_SendACK(aHandle, aErrorMgr) != ALA_SUCCESS)
                    {
                        printAlaErr(aErrorMgr);
                        (void)ALA_GetErrorLevel(aErrorMgr, &sErrorLevel);
                        if(sErrorLevel == ALA_ERROR_FATAL)
                        {
                            return ALA_FAILURE;
                        }
                        /* Wait and Handshake with XLog Sender */
                        break;
                    }
                }
            }   /* else */
        }   /* while */
    }   /* while */

    return ALA_SUCCESS;
}

ALA_RC applyXLogToAltibase(ALA_Handle aHandle, ALA_XLog * aXLog, ALA_ErrorMgr * aErrorMgr)
{
    ALA_Table      * sTable = NULL;
    ALA_XLogHeader * sXLogHeader = NULL;
    char             sQuery[QUERY_SIZE];


    /* Get XLog Header */
    (void)ALA_GetXLogHeader(aXLog,
                             (const ALA_XLogHeader **)&sXLogHeader,
                             aErrorMgr);

    /* if COMMIT XLog, then Commit Current Transaction */
    if(sXLogHeader->mType == XLOG_TYPE_COMMIT)
    {
        printf("commit\n");
    }
    /* if ABORT XLog, then Rollback Current Transaction */
    else if(sXLogHeader->mType == XLOG_TYPE_ABORT)
    {
        printf("abort\n");
    }
    /* if REPL_STOP XLog, then Rollback Current Transaction */
    else if(sXLogHeader->mType == XLOG_TYPE_REPL_STOP)
    {
        printf("repl_stop\n");
    }
    else
    {
        /* Get Table Information */
        if(ALA_GetTableInfo(aHandle,
                            sXLogHeader->mTableOID,
                            (const ALA_Table **)&sTable,
                            aErrorMgr) != ALA_SUCCESS)
        {
            printAlaErr(aErrorMgr);
            return ALA_FAILURE;
        }

        memset(sQuery, 0x00, QUERY_SIZE);
        if(ALA_GetAltibaseSQL(sTable, aXLog, QUERY_SIZE, (signed char *)sQuery, aErrorMgr)
           != ALA_SUCCESS)
        {
            printAlaErr(aErrorMgr);
            return ALA_FAILURE;
        }
        printf("%s\n",sQuery);

        mytest(sTable, aXLog);
    }

    return ALA_SUCCESS;
}

void mytest(ALA_Table * aTable, ALA_XLog * aXLog)
{
    ALA_Column * sColumn;
    SChar        sBuffer[1024];
    UInt         sPKColumnPos;
    UInt         sColumnPos;

    switch (aXLog->mHeader.mType)
    {
        case XLOG_TYPE_INSERT :
            printf("insert\n");
            break;

        case XLOG_TYPE_UPDATE :
            printf("update\n");
            break;

        case XLOG_TYPE_DELETE :
            printf("delete\n");
            break;

        default :
            printf("etc[%d]\n",aXLog->mHeader.mType);
            return;
            break;
    }

    
    for(sPKColumnPos = 0; sPKColumnPos < aTable->mPKColumnCount; sPKColumnPos++)
    {
        /* XLog의 Primary Key 순서와 Table의 Primary Key 순서는 동일 */
        sColumn = aTable->mPKColumnArray[sPKColumnPos];
        printf("table-pk[%s]\n", sColumn->mColumnName);
    }
    
    /* Primary Key Column 처리 */
    for(sPKColumnPos = 0; sPKColumnPos < aXLog->mPrimaryKey.mPKColCnt; sPKColumnPos++)
    {
        /* XLog의 Primary Key 순서와 Table의 Primary Key 순서는 동일 */
        sColumn = aTable->mPKColumnArray[sPKColumnPos];
        printf("pk[%s]\n", sColumn->mColumnName);
        
        /* Altibase Text 얻기 */
        (void)ALA_GetAltibaseText(sColumn, &(aXLog->mPrimaryKey.mPKColArray[sPKColumnPos]), 1024, sBuffer, NULL);
        printf("value[%s]\n", sBuffer);
    }
        
    /* Column 처리 */
    for(sColumnPos = 0; sColumnPos < aXLog->mColumn.mColCnt; sColumnPos++)
    {
        /* Column 정보 얻기 */
        (void)ALA_GetColumnInfo(aTable, aXLog->mColumn.mCIDArray[sColumnPos], &sColumn, NULL);
        printf("col[%s]\n", sColumn->mColumnName);
        
        /* After Image의 Altibase Text 얻기 */
        (void)ALA_GetAltibaseText(sColumn, &(aXLog->mColumn.mAColArray[sColumnPos]), 1024, sBuffer, NULL);
        printf("value[%s]\n", sBuffer);
    }
}

void printAlaErr(ALA_ErrorMgr * aErrorMgr)
{
    char * sErrorMessage = NULL;
    int    sErrorCode;

    (void)ALA_GetErrorCode(aErrorMgr, (unsigned int *)&sErrorCode);
    (void)ALA_GetErrorMessage(aErrorMgr, (const signed char **)&sErrorMessage);

    printf("ALA Error : %d, %s\n", sErrorCode, sErrorMessage);
}
