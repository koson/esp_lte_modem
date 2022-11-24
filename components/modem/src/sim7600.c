// Copyright 2020 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "bg96.h"
#include "bg96_private.h"

struct tm _sim_rtc;
int _sim_rtc_timezone = 7;

/**
 * @brief This module supports SIM7600 module, which has a very similar interface
 * to the BG96, so it just references most of the handlers from BG96 and implements
 * only those that differ.
 */
static const char *DCE_TAG = "sim7600";


/**
 * Remove leading whitespace characters from string
 */
void trimLeading(char * str)
{
    int index, i;
    index = 0;
    /* Find last index of whitespace character */
    while(str[index] == ' ' || str[index] == '\t' || str[index] == '\n')
    {
        index++;
    }
    if(index != 0)
    {
        /* Shift all trailing characters to its left */
        i = 0;
        while(str[i + index] != '\0')
        {
            str[i] = str[i + index];
            i++;
        }
        str[i] = '\0'; // Make sure that string is NULL terminated
    }
}




/**
 * @brief Handle response from AT+CCLK
 */
static esp_err_t sim7600_handle_cclk(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;
    bg96_modem_dce_t *bg96_dce = __containerof(dce, bg96_modem_dce_t, parent);
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    }
	if (!strncmp(line, "+CCLK", strlen("+CCLK")))
	{
		sscanf(line, "+CCLK: \"%2d/%2d/%2d,%2d:%2d:%2d%3d\"", &_sim_rtc.tm_year,
				&_sim_rtc.tm_mon, &_sim_rtc.tm_mday, &_sim_rtc.tm_hour,
				&_sim_rtc.tm_min, &_sim_rtc.tm_sec, &_sim_rtc_timezone);
        _sim_rtc.tm_year = (_sim_rtc.tm_year + 2000) - 1900;
        _sim_rtc_timezone = _sim_rtc_timezone / 4;

		printf("%4d/%02d/%02d,%02d:%02d:%02d(%3d)\r\n\n", _sim_rtc.tm_year + 1900,
				_sim_rtc.tm_mon, _sim_rtc.tm_mday, _sim_rtc.tm_hour,
				_sim_rtc.tm_min, _sim_rtc.tm_sec, _sim_rtc_timezone);

		err = ESP_OK;
    }
    return err;
}
/**
 * @brief Handle response from AT+CBC
 */
static esp_err_t sim7600_handle_cbc(modem_dce_t *dce, const char *line)
{
    esp_err_t err = ESP_FAIL;

    char* lines = (char*)line;
    trimLeading(lines);
    bg96_modem_dce_t *bg96_dce = __containerof(dce, bg96_modem_dce_t, parent);
    if (strstr(lines, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(lines, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    }
    if (!strncmp(lines, "+CBC", strlen("+CBC"))) {
        /* store value of bcs, bcl, voltage */
        int32_t **cbc = bg96_dce->priv_resource;
        int32_t volts = 0, fraction = 0;
        /* +CBC: <voltage in Volts> V*/
        sscanf(lines, "+CBC: %d.%dV", &volts, &fraction);
        /* Since the "read_battery_status()" API (besides voltage) returns also values for BCS, BCL (charge status),
         * which are not applicable to this modem, we return -1 to indicate invalid value
         */
        *cbc[0] = -1; // BCS
        *cbc[1] = -1; // BCL
        *cbc[2] = volts*1000 + fraction;
        err = ESP_OK;
    }
    return err;
}

/**
 * @brief Get battery status
 *
 * @param dce Modem DCE object
 * @param bcs Battery charge status
 * @param bcl Battery connection level
 * @param voltage Battery voltage
 * @return esp_err_t
 *      - ESP_OK on success
 *      - ESP_FAIL on error
 */
static esp_err_t sim7600_get_battery_status(modem_dce_t *dce, uint32_t *bcs, uint32_t *bcl, uint32_t *voltage)
{
    modem_dte_t *dte = dce->dte;
    bg96_modem_dce_t *bg96_dce = __containerof(dce, bg96_modem_dce_t, parent);
    uint32_t *resource[3] = {bcs, bcl, voltage};
    bg96_dce->priv_resource = resource;
    dce->handle_line = sim7600_handle_cbc;
    DCE_CHECK(dte->send_cmd(dte, "AT+CBC\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "inquire battery status failed", err);
    ESP_LOGI(DCE_TAG, "inquire battery status ok");
    return ESP_OK;
err:
    return ESP_FAIL;
}

static esp_err_t example_default_handle(modem_dce_t *dce, const char *line)
{
//    printf("===Rec Line : %s", line);
    esp_err_t err = ESP_FAIL;
    if (strstr(line, MODEM_RESULT_CODE_SUCCESS)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_SUCCESS);
    } else if (strstr(line, MODEM_RESULT_CODE_ERROR)) {
        err = esp_modem_process_command_done(dce, MODEM_STATE_FAIL);
    }
    return err;
}


esp_err_t sim7600_NetTimeSetup(modem_dce_t *dce)
{
    modem_dte_t *dte = dce->dte;
    // Test clock
    dce->handle_line = example_default_handle;
    DCE_CHECK(dte->send_cmd(dte, "AT+CCLK?\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "send AT+CCLK? failed", err);
    ESP_LOGI(DCE_TAG, "inquire clock ok");



//    // set fake time
//    dce->handle_line = example_default_handle;
//    DCE_CHECK(dte->send_cmd(dte,"AT+CCLK=\"22/01/01,12:20:12+28\"\r", 5000) == ESP_OK, "send command failed", err);
//    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "set fake time failed", err);
//    ESP_LOGI(DCE_TAG, "set fake time ok");



    // enable net time sync
    dce->handle_line = example_default_handle;
    DCE_CHECK(dte->send_cmd(dte, "AT+CNTP=\"ntp.time.nl\",28\r", 5000) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "send AT+CLTS=1 failed", err);
    ESP_LOGI(DCE_TAG, "inquire clock ok");


    // enable net time sync
    dce->handle_line = example_default_handle;
    DCE_CHECK(dte->send_cmd(dte, "AT+CNTP\r", 5000) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "send AT+CLTS=1 failed", err);
    ESP_LOGI(DCE_TAG, "inquire clock ok");




//    // save setting
//    dce->handle_line = example_default_handle;
//    DCE_CHECK(dte->send_cmd(dte, "AT&W\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
//    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "send AT&W failed", err);
//    ESP_LOGI(DCE_TAG, "Save NetTime ok");

    return ESP_OK;
err:
    return ESP_FAIL;
}

esp_err_t sim7600_get_NetTime(modem_dce_t *dce)
{
    modem_dte_t *dte = dce->dte;
    // Test clock
    dce->handle_line = sim7600_handle_cclk;
    DCE_CHECK(dte->send_cmd(dte, "AT+CCLK?\r", MODEM_COMMAND_TIMEOUT_DEFAULT) == ESP_OK, "send command failed", err);
    DCE_CHECK(dce->state == MODEM_STATE_SUCCESS, "send AT+CCLK? failed", err);
    ESP_LOGI(DCE_TAG, "inquire clock ok");

    return ESP_OK;
err:
    return ESP_FAIL;
}



/**
 * @brief Create and initialize SIM7600 object
 *
 */
modem_dce_t *sim7600_init(modem_dte_t *dte)
{
    modem_dce_t *dce = bg96_init(dte);
    dte->dce->get_battery_status = sim7600_get_battery_status;
    dte->dce->setup_cmux = esp_modem_dce_setup_cmux;
    return dce;
}
