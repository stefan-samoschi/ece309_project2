#include "core/conversation.h"
#include "core/sentinel_scanner.h"
#include "harness/harness.h"
#include "model/replay_client.h"
#include "model/scripted_client.h"

#include <cassert>
#include <stdexcept>
#include <string>
#include <utility>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string_view>
#include <vector>

void test_empty_bounds() {
    Conversation c;

    assert(c.size() == 0);
    assert(c.begin() == c.end());

    bool threw = false;
    try {
        c.at(0);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    assert(threw);
}

void test_system_order() {
    Conversation c;
    c.append(Message(Role::System, "rules"));
    c.append(Message(Role::User, "hello"));
    c.append(Message(Role::Assistant, "hi"));

    assert(c.size() == 3);
    assert(c.at(0).role() == Role::System);
    assert(c.at(0).content() == "rules");
    assert(c.at(1).role() == Role::User);
    assert(c.at(2).role() == Role::Assistant);
}

void test_copy_constructor() {
    Conversation original;
    original.append(Message(Role::User, "first"));

    Conversation copy(original);

    assert(copy.size() == 1);
    assert(copy.at(0).content() == "first");
    assert(copy.begin() != original.begin());

    original.append(Message(Role::User, "second"));
    assert(copy.size() == 1);
}

void test_copy_assignment() {
    Conversation source;
    source.append(Message(Role::User, "source"));

    Conversation destination;
    destination.append(Message(Role::User, "old"));
    destination = source;

    assert(destination.at(0).content() == "source");
    assert(destination.begin() != source.begin());

    destination = destination;  // Self-assignment must also be safe.
    assert(destination.at(0).content() == "source");
}

void test_move_constructor() {
    Conversation source;
    source.append(Message(Role::User, "hello"));
    const Message* old_address = source.begin();

    Conversation destination(std::move(source));

    assert(destination.begin() == old_address);
    assert(destination.at(0).content() == "hello");
    assert(source.begin() == nullptr);
    assert(source.size() == 0);
}

void test_move_assignment() {
    Conversation source;
    source.append(Message(Role::User, "new"));
    const Message* old_address = source.begin();

    Conversation destination;
    destination.append(Message(Role::User, "old"));
    destination = std::move(source);

    assert(destination.begin() == old_address);
    assert(destination.at(0).content() == "new");
    assert(source.begin() == nullptr);
    assert(source.size() == 0);
}

void test_growth_and_iteration() {
    Conversation c;

    for (int i = 0; i < 65; ++i) {
        c.append(Message(Role::User, std::to_string(i)));
        assert(c.size() == static_cast<std::size_t>(i + 1));
        assert(c.at(i).content() == std::to_string(i));
    }

    int expected = 0;
    for (const Message& m : c) {
        assert(m.content() == std::to_string(expected));
        ++expected;
    }
    assert(expected == 65);
}
void test_scanner_clean_text() {
    SentinelScanner scanner("<|end_conversation|>");

    auto part = scanner.feed("hello world");
    auto end = scanner.flush();

    assert(!part.sentinel_found);
    assert(!end.sentinel_found);
    assert(part.safe_text + end.safe_text == "hello world");
}

void test_scanner_whole_sentinel() {
    SentinelScanner scanner("<|end_conversation|>");

    auto out = scanner.feed(
        "Goodbye.<|end_conversation|>text after the sentinel"
    );

    assert(out.sentinel_found);
    assert(out.safe_text == "Goodbye.");
    assert(scanner.flush().safe_text.empty());
}

void test_scanner_every_split() {
    const std::string sentinel = "<|end_conversation|>";
    const std::string text = "Goodbye." + sentinel;

    for (std::size_t split = 0; split <= text.size(); ++split) {
        SentinelScanner scanner(sentinel);

        auto first = scanner.feed(text.substr(0, split));
        auto second = scanner.feed(text.substr(split));

        assert(first.sentinel_found || second.sentinel_found);
        assert(first.safe_text + second.safe_text == "Goodbye.");
    }
}

void test_scanner_one_character_at_a_time() {
    SentinelScanner scanner("<|end_conversation|>");
    const std::string text = "yes<|end_conversation|>";
    std::string visible;

    for (char ch : text) {
        auto out = scanner.feed(std::string(1, ch));
        visible += out.safe_text;
    }

    assert(visible == "yes");
    assert(scanner.feed("").sentinel_found);
}

void test_scanner_false_alarms_and_overlap() {
    SentinelScanner scanner("<|end_conversation|>");
    const std::string text = "<|end_world|><|end_conversatioX|>";
    std::string visible;

    for (char ch : text) {
        visible += scanner.feed(std::string(1, ch)).safe_text;
    }
    visible += scanner.flush().safe_text;

    assert(visible == text);

    SentinelScanner overlapping("aab");
    auto out = overlapping.feed("aaaab");
    assert(out.sentinel_found);
    assert(out.safe_text == "aa");
}

void test_scanner_large_stream() {
    SentinelScanner scanner("<|end_conversation|>");
    std::size_t characters_emitted = 0;

    for (int i = 0; i < 4 * 1024 * 1024; ++i) {
        auto out = scanner.feed("<");
        assert(!out.sentinel_found);
        characters_emitted += out.safe_text.size();
    }

    characters_emitted += scanner.flush().safe_text.size();
    assert(characters_emitted == 4u * 1024 * 1024);
}
class TestInput : public InputSource {
public:
    explicit TestInput(std::vector<std::string> lines)
        : lines_(std::move(lines)) {}

    std::string read_line() override {
        if (next_ == lines_.size()) {
            eof_ = true;
            return "";
        }
        return lines_[next_++];
    }

    bool is_eof() const override {
        return eof_;
    }

private:
    std::vector<std::string> lines_;
    std::size_t next_ = 0;
    bool eof_ = false;
};

class TestOutput : public OutputSink {
public:
    void write(std::string_view text) override {
        result += text;
    }

    std::string result;
};

std::string make_test_file(const std::string& content) {
    static int number = 0;
    std::string path = "p2_test_" + std::to_string(++number) + ".txt";

    std::ofstream file(path);
    file << content;
    return path;
}

void test_harness_turn_limit() {
    std::string path = make_test_file(
        "role: assistant\nOne\n"
        "---\n"
        "role: assistant\nTwo\n"
    );

    Harness harness(
        std::make_unique<ScriptedModelClient>(path),
        HarnessConfig{2, "rules"}
    );
    TestInput input({"hi", "again"});
    TestOutput output;

    StopReason reason = harness.run(input, output);

    assert(reason.kind == StopReason::Kind::TurnLimit);
    assert(harness.conversation().size() == 5);
    assert(harness.conversation().at(0).role() == Role::System);

    std::remove(path.c_str());
}

void test_harness_sentinel_stop() {
    std::string path = make_test_file(
        "chunk: 1\n"
        "role: assistant\n"
        "Bye<|end_conversation|>ignored\n"
    );

    Harness harness(
        std::make_unique<ScriptedModelClient>(path),
        HarnessConfig{3, ""}
    );
    TestInput input({"hi"});
    TestOutput output;

    StopReason reason = harness.run(input, output);

    assert(reason.kind == StopReason::Kind::Sentinel);
    assert(output.result.find("<|end_conversation|>") == std::string::npos);
    assert(harness.conversation().at(1).content() ==
           "Bye<|end_conversation|>");

    std::remove(path.c_str());
}

void test_harness_user_eof() {
    std::string path = make_test_file(
        "role: assistant\nunused\n"
    );

    Harness harness(
        std::make_unique<ScriptedModelClient>(path),
        HarnessConfig{2, ""}
    );
    TestInput input({});
    TestOutput output;

    StopReason reason = harness.run(input, output);
    assert(reason.kind == StopReason::Kind::UserExit);

    std::remove(path.c_str());
}
void test_transcript_round_trip() {
    std::string script_path = make_test_file(
        "role: assistant\nHello\n"
        "---\n"
        "chunk: 2\n"
        "role: assistant\nBye<|end_conversation|>\n"
    );

    Harness original(
        std::make_unique<ScriptedModelClient>(script_path),
        HarnessConfig{4, "rules"}
    );
    TestInput first_input({"hi", "bye"});
    TestOutput first_output;

    assert(original.run(first_input, first_output).kind ==
           StopReason::Kind::Sentinel);

    // Save the conversation using the provided transcript format.
    std::string transcript_path = make_test_file("");
    {
        std::ofstream file(transcript_path);
        bool first_block = true;

        for (const Message& message : original.conversation()) {
            if (!first_block) {
                file << "---\n";
            }
            first_block = false;

            const char* role =
                message.role() == Role::System ? "system" :
                message.role() == Role::User ? "user" : "assistant";

            file << "role: " << role << "\n";
            file << message.content() << "\n";
        }
    }

    auto replay_client =
        std::make_unique<ReplayModelClient>(transcript_path);
    assert(replay_client->system_message() == "rules");

    Harness replay(
        std::move(replay_client),
        HarnessConfig{4, "rules"}
    );
    TestInput second_input({"hi", "bye"});
    TestOutput second_output;

    assert(replay.run(second_input, second_output).kind ==
           StopReason::Kind::Sentinel);
    assert(second_output.result == first_output.result);

    const Conversation& a = original.conversation();
    const Conversation& b = replay.conversation();
    assert(a.size() == b.size());

    for (std::size_t i = 0; i < a.size(); ++i) {
        assert(a.at(i).role() == b.at(i).role());
        assert(a.at(i).content() == b.at(i).content());
    }

    std::remove(script_path.c_str());
    std::remove(transcript_path.c_str());
}

int main() {
    test_empty_bounds();
    test_system_order();
    test_copy_constructor();
    test_copy_assignment();
    test_move_constructor();
    test_move_assignment();
    test_growth_and_iteration();
    test_scanner_clean_text();
    test_scanner_whole_sentinel();
    test_scanner_every_split();
    test_scanner_one_character_at_a_time();
    test_scanner_false_alarms_and_overlap();
    test_scanner_large_stream();
    test_harness_turn_limit();
    test_harness_sentinel_stop();
    test_harness_user_eof();
    test_transcript_round_trip();
}