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
    cJSON *sensors = cJSON_CreateArray();
    cJSON *id1 = cJSON_CreateObject();
    cJSON *id2 = cJSON_CreateObject();
    cJSON *iNumber = cJSON_CreateNumber(10);

    cJSON_AddItemToObject(id1, "id", cJSON_CreateString("1"));
    cJSON_AddItemToObject(id1, "temperature1", cJSON_CreateString("23"));
    cJSON_AddItemToObject(id1, "temperature2", cJSON_CreateString("23"));
    cJSON_AddItemToObject(id1, "humidity", cJSON_CreateString("55"));
    cJSON_AddItemToObject(id1, "occupancy", cJSON_CreateString("1"));
    cJSON_AddItemToObject(id1, "illumination", cJSON_CreateString("23"));

    cJSON_AddItemToObject(id2, "id", cJSON_CreateString("2"));
    cJSON_AddItemToObject(id2, "temperature1", cJSON_CreateString("23"));
    cJSON_AddItemToObject(id2, "temperature2", cJSON_CreateString("23"));
    cJSON_AddItemToObject(id2, "humidity", cJSON_CreateString("55"));
    cJSON_AddItemToObject(id2, "occupancy", cJSON_CreateString("1"));
    cJSON_AddItemToObject(id2, "illumination", cJSON_CreateString("23"));

    cJSON_AddItemToObject(id2, "value", iNumber);

    cJSON_AddItemToArray(sensors, id1);
    cJSON_AddItemToArray(sensors, id2);

    cJSON_AddItemToObject(root, "sensors", sensors);
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
