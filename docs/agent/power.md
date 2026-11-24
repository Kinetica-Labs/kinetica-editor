# Питание: мотор постоянного тока, батарея, драйвер

Порядок — сначала мотор, потом питание:

1. `assembly_drive {joint, kind: "dcmotor", voltage, resistance, constant}` — мотор на главной степени свободы сустава
   (напряжение В, сопротивление обмотки Ом, постоянная Н·м/А = В·с/рад). Знак `voltage` — направление вращения.
2. `assembly_power {joint}` — драйвер (H-мост) и батарея к нему; отвечает id драйвера и `battery` — id батареи.
   Без мотора на суставе — отказ, который говорит, что сделать сначала.

```json
{"ops": [
  {"op": "assembly_drive", "args": {"joint": 7, "kind": "dcmotor", "voltage": -11.1, "resistance": 0.13, "constant": 0.29}},
  {"as": "питание", "op": "assembly_power", "args": {"joint": 7}},
  {"op": "scene_set", "args": {"id": "$питание.battery", "line": "cells", "key": "parallel", "values": [3]}},
  {"op": "scene_set", "args": {"id": "$питание", "line": "bridge", "key": "limit", "values": [15]}}]}
```

- Батарея: строка `cells`, ключи `series` и `parallel` (сколько ячеек последовательно и параллельно), `capacity`
  (А·ч одной ячейки).
- Драйвер: строка `bridge`, ключи `limit` (предел тока, А) и `duty` (скважность −1…1). Новый драйвер стоит на
  скважности 0 — мотор молчит, пока её не задаст код или пульт.
- Пока сцена играет, `sim_measure {scene: true}` даёт каналы `power/<имя батареи> charge, voltage, current,
  energy, heat` и `power/<имя драйвера> current, heat, work`.
