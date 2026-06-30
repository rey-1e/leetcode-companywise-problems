#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <iomanip>

namespace fs = std::filesystem;

// Trim leading and trailing whitespace
std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

// Parse a single CSV line, handling quotes and escaped quotes
std::vector<std::string> parse_csv_line(const std::string& line) {
    std::vector<std::string> result;
    std::string current;
    bool in_quotes = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') {
            if (in_quotes && i + 1 < line.size() && line[i + 1] == '"') {
                current += '"';
                ++i; // skip next quote
            } else {
                in_quotes = !in_quotes;
            }
        } else if (c == ',' && !in_quotes) {
            result.push_back(current);
            current.clear();
        } else if ((c == '\r' || c == '\n') && !in_quotes) {
            // Ignore newlines
        } else {
            current += c;
        }
    }
    result.push_back(current);
    return result;
}

// Escape JSON control characters and quotes
std::string escape_json_string(const std::string& input) {
    std::string output;
    for (char c : input) {
        if (c == '"') {
            output += "\\\"";
        } else if (c == '\\') {
            output += "\\\\";
        } else if (c == '\b') {
            output += "\\b";
        } else if (c == '\f') {
            output += "\\f";
        } else if (c == '\n') {
            output += "\\n";
        } else if (c == '\r') {
            output += "\\r";
        } else if (c == '\t') {
            output += "\\t";
        } else if (static_cast<unsigned char>(c) < 32) {
            char buf[7];
            snprintf(buf, sizeof(buf), "\\u%04x", c);
            output += buf;
        } else {
            output += c;
        }
    }
    return output;
}

struct Question {
    std::string id;
    std::string title;
    std::string acceptance;
    std::string difficulty;
    std::string link;
};

int main() {
    std::string src_dir = "leetcode-companywise-interview-questions";
    std::string dest_dir = "data";

    if (!fs::exists(src_dir)) {
        std::cerr << "Error: Source directory '" << src_dir << "' does not exist." << std::endl;
        return 1;
    }

    try {
        fs::create_directories(dest_dir);
    } catch (const std::exception& e) {
        std::cerr << "Error creating directory '" << dest_dir << "': " << e.what() << std::endl;
        return 1;
    }

    std::vector<std::string> companies;

    // Collect and sort directory entries
    std::vector<fs::directory_entry> entries;
    for (const auto& entry : fs::directory_iterator(src_dir)) {
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
        return a.path().filename().string() < b.path().filename().string();
    });

    for (const auto& entry : entries) {
        if (!entry.is_directory()) continue;

        std::string company_name = entry.path().filename().string();
        if (company_name == ".git" || company_name == "src") continue;

        // Choose CSV file: prioritize all.csv, then any other CSV
        fs::path csv_path = entry.path() / "all.csv";
        if (!fs::exists(csv_path)) {
            std::vector<fs::path> other_csvs;
            for (const auto& file : fs::directory_iterator(entry.path())) {
                if (file.is_regular_file() && file.path().extension() == ".csv") {
                    other_csvs.push_back(file.path());
                }
            }
            if (other_csvs.empty()) continue;
            std::sort(other_csvs.begin(), other_csvs.end());
            csv_path = other_csvs[0];
        }

        std::ifstream file(csv_path, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Warning: Could not open " << csv_path << std::endl;
            continue;
        }

        std::string line;
        // Read header
        if (!std::getline(file, line)) continue;

        // Strip UTF-8 BOM if present
        if (line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line = line.substr(3);
        }

        std::vector<std::string> headers = parse_csv_line(line);
        int id_idx = -1, title_idx = -1, acc_idx = -1, diff_idx = -1, url_idx = -1;

        for (size_t i = 0; i < headers.size(); ++i) {
            std::string h = trim(headers[i]);
            if (h == "ID") id_idx = i;
            else if (h == "Title") title_idx = i;
            else if (h == "Acceptance %") acc_idx = i;
            else if (h == "Difficulty") diff_idx = i;
            else if (h == "URL") url_idx = i;
        }

        if (id_idx == -1 || title_idx == -1 || acc_idx == -1 || diff_idx == -1 || url_idx == -1) {
            std::cerr << "Warning: Skipping " << csv_path << " due to missing columns." << std::endl;
            continue;
        }

        std::vector<Question> questions;
        while (std::getline(file, line)) {
            std::vector<std::string> row = parse_csv_line(line);
            size_t max_idx = std::max({id_idx, title_idx, acc_idx, diff_idx, url_idx});
            if (row.size() <= max_idx) continue;

            std::string id = trim(row[id_idx]);
            std::string title = trim(row[title_idx]);
            if (id.empty() || title.empty()) continue;

            questions.push_back({
                id,
                title,
                trim(row[acc_idx]),
                trim(row[diff_idx]),
                trim(row[url_idx])
            });
        }

        if (questions.empty()) continue;

        // Write company JSON
        fs::path json_path = fs::path(dest_dir) / (company_name + ".json");
        std::ofstream json_out(json_path);
        if (!json_out.is_open()) {
            std::cerr << "Error writing " << json_path << std::endl;
            continue;
        }

        json_out << "[\n";
        for (size_t i = 0; i < questions.size(); ++i) {
            const auto& q = questions[i];
            json_out << "  {\n"
                     << "    \"id\": \"" << escape_json_string(q.id) << "\",\n"
                     << "    \"title\": \"" << escape_json_string(q.title) << "\",\n"
                     << "    \"acceptance\": \"" << escape_json_string(q.acceptance) << "\",\n"
                     << "    \"difficulty\": \"" << escape_json_string(q.difficulty) << "\",\n"
                     << "    \"link\": \"" << escape_json_string(q.link) << "\"\n"
                     << "  }" << (i + 1 < questions.size() ? "," : "") << "\n";
        }
        json_out << "]";
        json_out.close();

        companies.push_back(company_name);
    }

    // Write companies.json
    fs::path companies_json_path = fs::path(dest_dir) / "companies.json";
    std::ofstream comp_out(companies_json_path);
    if (!comp_out.is_open()) {
        std::cerr << "Error writing " << companies_json_path << std::endl;
        return 1;
    }

    std::sort(companies.begin(), companies.end());

    comp_out << "[\n";
    for (size_t i = 0; i < companies.size(); ++i) {
        comp_out << "  \"" << escape_json_string(companies[i]) << "\"" << (i + 1 < companies.size() ? "," : "") << "\n";
    }
    comp_out << "]";
    comp_out.close();

    std::cout << "Successfully processed " << companies.size() << " companies." << std::endl;
    return 0;
}
