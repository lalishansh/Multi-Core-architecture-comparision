#pragma once

#include "logging.hxx"

#define BIND_FUNCTION(fn) [this] (auto&&... args)->decltype (auto) { return this->fn (std::forward<decltype (args)> (args)...); }