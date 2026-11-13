# Referencias · ESP-NOW Demos

Documentación externa que se usó para entender el hardware, los protocolos y las
libertades del proyecto. No es una bibliografía decorativa: cada enlace apunta a
la fuente que explica una decisión concreta.

## Fuentes técnicas

| Fuente | Para qué se consultó |
|---|---|
| [ESP-NOW](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html) | Protocolo sin conexión y su API. |
| [arduino-esp8266](https://arduino-esp8266.readthedocs.io/en/latest/) | Core Arduino para ESP8266. |
| [arduino-esp32](https://docs.espressif.com/projects/arduino-esp32/en/latest/) | Core Arduino para ESP32. |
| [LoRaWAN 1.0.4](https://lora-alliance.org/resource_hub/lorawan-specification-v1-0-4/) | Especificación del protocolo LoRaWAN. |

## Versiones de este repositorio

`espNowDemo`, `espNowDemoLoRaWAN1.1`, `red_espnow_8266_32`

Cada carpeta puede tener dependencias y versiones distintas. Cuando el código
depende de una versión específica, esa versión está declarada en el README o en
el archivo de configuración de la carpeta, no en este documento.

## Criterio de preservación

- El código y los documentos se copiaron desde la carpeta original de trabajo.
- Se excluyeron `.git`, dependencias instaladas, compilados, registros y bases
  de datos locales.
- Se redactaron tokens, claves privadas y contraseñas detectadas en texto.
- Las plantillas `.example`, `.sample` y los valores de demostración se
  conservan porque documentan la configuración esperada.

## Limitaciones conocidas

Una demo de sensores en una feria o en una sala no puede depender del WiFi del lugar ni de una nube que a veces no carga.

La documentación de cada versión describe el alcance real. Si una función no
está implementada, se dice; si un hardware no fue probado, también.
