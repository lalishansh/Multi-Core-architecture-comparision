#pragma once

#include <fmt/format.h>;
#include <cstdio>;
#include <source_location>;
#include <filesystem>;

// TODO: add asserts and platform, build type detection code etc.
#define LOG_raw(...)		{ fmt::print (stderr, __VA_ARGS__); }
#define LOG_trace(...)		{ fmt::print (stderr, " \n[LOG_TRACE L{:d}\tF: {:s}]\t{:s}", __LINE__, std::filesystem::relative (__FILE__, PROJECT_ROOT_LOCATION).generic_string (), fmt::format (__VA_ARGS__)); }
#define LOG_debug(...)		{ fmt::print (stderr, " \n[LOG_DEBUG L{:d}\tF: {:s}]\t{:s}", __LINE__, std::filesystem::relative (__FILE__, PROJECT_ROOT_LOCATION).generic_string (), fmt::format (__VA_ARGS__)); }
#define LOG_info(...)		{ fmt::print (stderr, " \n[LOG_INFO  L{:d}\tF: {:s}]\t{:s}", __LINE__, std::filesystem::relative (__FILE__, PROJECT_ROOT_LOCATION).generic_string (), fmt::format (__VA_ARGS__)); }
#define LOG_warn(...)		{ fmt::print (stderr, " \n[LOG_WARN  L{:d}\tF: {:s}]\t{:s}", __LINE__, std::filesystem::relative (__FILE__, PROJECT_ROOT_LOCATION).generic_string (), fmt::format (__VA_ARGS__)); }
#define LOG_error(...)		{ fmt::print (stderr, " \n[LOG_ERROR L{:d}\tF: {:s}]\t{:s}", __LINE__, std::filesystem::relative (__FILE__, PROJECT_ROOT_LOCATION).generic_string (), fmt::format (__VA_ARGS__)); }

#define LOG_CORE_trace(...)		 { fmt::print (stderr, " \n[CORE LOG_TRACE L{:d}\tF: {:s}]\t{:s}", __LINE__, std::filesystem::relative (__FILE__, PROJECT_ROOT_LOCATION).generic_string (), fmt::format (__VA_ARGS__)); }
#define LOG_CORE_debug(...)		 { fmt::print (stderr, " \n[CORE LOG_DEBUG L{:d}\tF: {:s}]\t{:s}", __LINE__, std::filesystem::relative (__FILE__, PROJECT_ROOT_LOCATION).generic_string (), fmt::format (__VA_ARGS__)); }
#define LOG_CORE_info(...)		 { fmt::print (stderr, " \n[CORE LOG_INFO  L{:d}\tF: {:s}]\t{:s}", __LINE__, std::filesystem::relative (__FILE__, PROJECT_ROOT_LOCATION).generic_string (), fmt::format (__VA_ARGS__)); }
#define LOG_CORE_warn(...)		 { fmt::print (stderr, " \n[CORE LOG_WARN  L{:d}\tF: {:s}]\t{:s}", __LINE__, std::filesystem::relative (__FILE__, PROJECT_ROOT_LOCATION).generic_string (), fmt::format (__VA_ARGS__)); }
#define LOG_CORE_error(...)		 { fmt::print (stderr, " \n[CORE LOG_ERROR L{:d}\tF: {:s}]\t{:s}", __LINE__, std::filesystem::relative (__FILE__, PROJECT_ROOT_LOCATION).generic_string (), fmt::format (__VA_ARGS__)); }

#define THROW_critical(...)	{ throw std::runtime_error (fmt::format (" \n[LOG_FATAL L{:d}\tF: {:s}]\t{:s}", __LINE__, std::filesystem::relative (__FILE__, PROJECT_ROOT_LOCATION).generic_string (), fmt::format (__VA_ARGS__))); }
#define THROW_CORE_critical(...) { throw std::runtime_error (fmt::format (" \n[CORE LOG_FATAL L{:d}\tF: {:s}]\t{:s}", __LINE__, std::filesystem::relative (__FILE__, PROJECT_ROOT_LOCATION).generic_string (), fmt::format (__VA_ARGS__))); }