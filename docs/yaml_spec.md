# YAML Specification (v0)

## Root Keys
- `app`
- `fabrication`

## app fields
- `device_type`: light|switch|outlet|custom
- `device_name`: string
- `flash_size`: string
- `network.connectivity`: wifi|thread
- `buttons`: list
- `led_strip`: config for WS2812/SK6812/APA106
- `endpoints`: list of Matter endpoints

## fabrication fields
- `port`: serial port
- `chip_target`: esp32c6|esp32c3|...
- `vendor_id`, `product_id`, `device_type_id`: hex strings
- `hardware_version`: string or integer

## Example
(See examples/light.yaml)
