#include "diagnostics/json_writer.hpp"

#include <catch2/catch_test_macros.hpp>

using goggles::diagnostics::JsonWriter;

TEST_CASE("JsonWriter empty object", "[json_writer]") {
    JsonWriter w;
    w.begin_object();
    w.end_object();
    REQUIRE(w.str() == "{}");
}

TEST_CASE("JsonWriter scalar values", "[json_writer]") {
    JsonWriter w;
    w.begin_object();
    w.key("int_val");
    w.value(42);
    w.key("float_val");
    w.value(3.14);
    w.key("bool_true");
    w.value(true);
    w.key("bool_false");
    w.value(false);
    w.key("null_val");
    w.value_null();
    w.end_object();

    auto s = w.str();
    REQUIRE(s.find("\"int_val\":42") != std::string::npos);
    REQUIRE(s.find("\"float_val\":3.14") != std::string::npos);
    REQUIRE(s.find("\"bool_true\":true") != std::string::npos);
    REQUIRE(s.find("\"bool_false\":false") != std::string::npos);
    REQUIRE(s.find("\"null_val\":null") != std::string::npos);
}

TEST_CASE("JsonWriter string escaping", "[json_writer]") {
    JsonWriter w;
    w.begin_object();
    w.key("escaped");
    w.value_string("line1\nline2\ttab\"quote\\back");
    w.end_object();

    auto s = w.str();
    REQUIRE(s.find(R"("escaped":"line1\nline2\ttab\"quote\\back")") != std::string::npos);
}

TEST_CASE("JsonWriter arrays", "[json_writer]") {
    JsonWriter w;
    w.begin_array();
    w.value(1);
    w.value(2);
    w.value(3);
    w.end_array();
    REQUIRE(w.str() == "[1,2,3]");
}

TEST_CASE("JsonWriter empty array", "[json_writer]") {
    JsonWriter w;
    w.begin_array();
    w.end_array();
    REQUIRE(w.str() == "[]");
}

TEST_CASE("JsonWriter nested object in array", "[json_writer]") {
    JsonWriter w;
    w.begin_array();
    w.begin_object();
    w.key("pass");
    w.value(0);
    w.end_object();
    w.begin_object();
    w.key("pass");
    w.value(1);
    w.end_object();
    w.end_array();

    auto s = w.str();
    REQUIRE(s.find("[{") != std::string::npos);
    REQUIRE(s.find("},{") != std::string::npos);
    REQUIRE(s.find("}]") != std::string::npos);
}

TEST_CASE("JsonWriter uint32 values", "[json_writer]") {
    JsonWriter w;
    w.begin_object();
    w.key("large");
    w.value(static_cast<uint32_t>(4294967295U));
    w.end_object();
    REQUIRE(w.str().find("4294967295") != std::string::npos);
}
