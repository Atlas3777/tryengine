#pragma once

//! C-API модуля live-хоста из SDK (`liblibDasModuleLiveHost.a`), которую хост использует
//! сам. Приложение регистрирует модуль статически (DAS::LiveHost), поэтому эти символы
//! приходят из той же копии, что и скриптовые `is_reload()`/`get_last_error()`.
//!
//! Заголовок SDK не поставляет, поэтому объявления повторяют экспортируемые функции
//! `extern "C"`; менять их можно только вместе с ABI SDK.
extern "C" {

//! Скриптовое `is_reload()`: ставится в начале цикла перезагрузки, до хуков.
void live_host_set_is_reload(bool value);

//! Скриптовое `get_reload_generation()`: растёт на каждом завершённом reload/reset.
void live_host_bump_reload_generation();

//! Скриптовое `get_last_error()`: текст ошибки компиляции или хука.
void live_host_set_last_error(const char* error);
void live_host_clear_error();

}  // extern "C"
