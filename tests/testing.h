#ifndef TESTING_H
#define TESTING_H

#include <stdexcept>
#include <sstream>

class TestFailed : public std::runtime_error {
public:
    TestFailed(const std::string &message)
    : std::runtime_error(message)
    { }
};

static void assert_equal(int left, int right, const std::string &message) {
    if (left != right) {
        std::stringstream ss;
        ss << message << ": " << left << " does not equal " << right << '.';
        throw TestFailed(ss.str());
    }
}

static void assert_equal(const std::string &left, const std::string &right, const std::string &message) {
    if (left != right) {
        std::stringstream ss;
        ss << message << ": " << left << " does not equal " << right << '.';
        throw TestFailed(ss.str());
    }
}

static void assert_true(bool value, const std::string &message) {
    if (!value) {
        std::stringstream ss;
        ss << message << '.';
        throw TestFailed(ss.str());
    }
}

#endif
