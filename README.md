## Слои библиотеки

1. `xxrf::core`:
    - RAII-wrapper для ресурсов библиотеки libhackrf: xxrf::Context, xxrf::Device
    - Конфигурация (frequency, sample rate, усиления, фильтр, clkout/clkin)
    - Async callbacks
2. `xxrf::stream`:
    - Реализация механизма потока для RX с ring-buffers и backpressure
    - Управление жизненным циклом
3. `xxrf::sync`:
    - clocks + hw trigger