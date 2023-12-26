#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <syncstream>
#include <vector>

const size_t TOPK = 10;
const size_t TEST_COUNT = 5;

using Counter = std::map<std::string, std::size_t>;

bool count_files_sync(char* filenames[], Counter& freq_dict);
bool count_files_async(char* filenames[], Counter& freq_dict);
bool count_files_thread(char* filenames[], Counter& freq_dict);

bool count_files_test(std::string test_name, char* filenames[],
                      bool (*count_files)(char**, Counter&));

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: topk_words [FILES...]\n";
        return EXIT_FAILURE;
    }

    auto filenames = &argv[1];

    auto res = count_files_test("Sync test", filenames, count_files_sync) &&
               count_files_test("Async test", filenames, count_files_async) &&
               count_files_test("Threaded test", filenames, count_files_thread);

    return res ? EXIT_SUCCESS : EXIT_FAILURE;
}

void print_topk(std::ostream& stream, const Counter& counter, size_t k) {
    if (counter.size() == 0) {
        stream << "Empty result\n";
        return;
    }

    k = std::min(k, counter.size());

    std::vector<Counter::const_iterator> words;
    words.reserve(counter.size());
    for (auto it = std::cbegin(counter); it != std::cend(counter); ++it) {
        words.push_back(it);
    }

    std::partial_sort(
        std::begin(words), std::begin(words) + k, std::end(words),
        [](auto lhs, auto& rhs) { return lhs->second > rhs->second; });

    std::for_each(std::begin(words), std::begin(words) + k,
                  [&stream](const Counter::const_iterator& pair) {
                      stream << std::setw(4) << pair->second << " "
                             << pair->first << '\n';
                  });
}

bool count_files_test(std::string test_name, char* filenames[],
                      bool (*count_files)(char**, Counter&)) {

    std::cout << "Starting test: " << test_name << "\n";

    auto start = std::chrono::high_resolution_clock::now();
    Counter freq_dict;

    for (int i = 0; i < TEST_COUNT; ++i) {
        freq_dict.clear();
        //std::cout << "Iteration: " << i << "\n";
        if (!count_files(filenames, freq_dict)) {
            std::cout << "Test failed\n\n";
            return false;
        }
    }

    print_topk(std::cout, freq_dict, TOPK);
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    std::cout << "Avarage elapsed time is " << elapsed_ms.count() / TEST_COUNT
              << " us\n\n";

    return true;
}

std::string tolower(const std::string& str) {
    std::string lower_str;
    std::transform(std::cbegin(str), std::cend(str),
                   std::back_inserter(lower_str),
                   [](unsigned char ch) { return (char)std::tolower(ch); });
    return lower_str;
};

void count_words(std::istream& stream, Counter& counter) {
    std::for_each(std::istream_iterator<std::string>(stream),
                  std::istream_iterator<std::string>(),
                  [&counter](const std::string& s) { ++counter[tolower(s)]; });
}

bool count_file(std::string filename, Counter& freq_dict) {
 /*   {
        std::osyncstream synccerr(std::cerr);
        synccerr << "Count_file " << filename << '\n';
    }*/

    std::ifstream input{filename};
    if (!input.is_open()) {
        std::osyncstream synccerr(std::cerr);
        synccerr << "Failed to open file " << filename << '\n';
        return false;
    }
    count_words(input, freq_dict);
    return true;
}

bool count_files_sync(char* filenames[], Counter& freq_dict) {
    for (int i = 0; filenames[i]; ++i) {
        if (!count_file(filenames[i], freq_dict)) {
            return false;
        }
    }

    return true;
}

bool threadsafe_count_file(const char* filename, Counter& freq_dict) {
    static std::mutex m;

    Counter current_freq_dict;
    if (count_file(filename, current_freq_dict)) {

        for (auto& dict_item_pair : current_freq_dict) {
            std::lock_guard g{m};
            freq_dict[dict_item_pair.first] += dict_item_pair.second;
        }

        return true;
    };

    return false;
}

bool count_files_async(char* filenames[], Counter& freq_dict) {
    std::vector<std::future<bool>> results;

    for (int i = 0; filenames[i]; ++i) {
        auto f = std::async(std::launch::async, [filenames, i, &freq_dict]() {
            return threadsafe_count_file(filenames[i], freq_dict);
        });

        results.push_back(std::move(f));
    }

    bool total_result = true;

    for (auto& res : results) {
        total_result = res.get() && total_result;
    }

    return total_result;
}

bool count_files_thread(char* filenames[], Counter& freq_dict) {
    std::vector<std::thread> results;
    std::atomic total_result{true};

    for (int i = 0; filenames[i]; ++i) {
        results.emplace_back([filenames, i, &freq_dict, &total_result]() {
            if (!threadsafe_count_file(filenames[i], freq_dict)) {
                total_result = false;
            }
        });
    }

    for (auto& res : results) {
        if (res.joinable()) {
            res.join();
        }
    }

    return total_result;
}