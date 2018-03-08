/* combine JSON package Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "cJSON.h"
#include <string.h>

void test_json(void) {
    uint8_t json[512] = {0};

    cJSON *root = cJSON_CreateObject();

    cJSON *id = cJSON_CreateObject();
    cJSON *params = cJSON_CreateObject();

    cJSON *version = cJSON_CreateObject();
    cJSON *iNumber = cJSON_CreateNumber(1);

    cJSON_AddItemToObject(root, "id", iNumber);

    cJSON_AddItemToObject(version, "version", cJSON_CreateString("20180307"));
    cJSON_AddItemToObject(root, "params", version);


    char *str = cJSON_Print(root);

    uint32_t jslen = strlen(str);
    memcpy(json, str, jslen);
    printf("%s\n", json);

    cJSON_Delete(root);
    free(str);
    str = NULL;
}


void app_main()
{
    printf("test json...\n");

	while(1){
		vTaskDelay(1000/portTICK_RATE_MS);
		printf("heap size:%d\n", esp_get_free_heap_size());
		test_json();
	}

}
