#include "homein-ref-parser.hpp"
#include <unordered_set>
#include <algorithm>
#include <cctype>

static bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
            return false;
    }
    return true;
}

static const std::unordered_set<std::string> BIBLE_BOOKS = {
    "Genesis", "Gen", "Exodus", "Exo", "Leviticus", "Lev", "Numbers", "Num",
    "Deuteronomy", "Deut", "Joshua", "Josh", "Judges", "Judg", "Ruth",
    "1 Samuel", "2 Samuel", "1 Kings", "2 Kings",
    "1 Chronicles", "2 Chronicles", "Ezra", "Nehemiah", "Esther",
    "Job", "Psalms", "Psa", "Proverbs", "Prov", "Ecclesiastes", "Ecc",
    "Song of Solomon", "Isaiah", "Isa", "Jeremiah", "Jer",
    "Lamentations", "Lam", "Ezekiel", "Eze", "Daniel", "Dan",
    "Hosea", "Hos", "Joel", "Amos", "Obadiah", "Oba", "Jonah", "Jon",
    "Micah", "Mic", "Nahum", "Nah", "Habakkuk", "Hab",
    "Zephaniah", "Zep", "Haggai", "Hag", "Zechariah", "Zec",
    "Malachi", "Mal", "Matthew", "Matt", "Mark", "Luke", "John",
    "Acts", "Romans", "Rom", "1 Corinthians", "1 Cor",
    "2 Corinthians", "2 Cor", "Galatians", "Gal", "Ephesians", "Eph",
    "Philippians", "Phil", "Colossians", "Col",
    "1 Thessalonians", "1 Thes", "2 Thessalonians", "2 Thes",
    "1 Timothy", "1 Tim", "2 Timothy", "2 Tim",
    "Titus", "Philemon", "Phlm", "Hebrews", "Heb", "James", "Jas",
    "1 Peter", "1 Pet", "2 Peter", "2 Pet",
    "1 John", "2 John", "3 John", "Jude", "Revelation", "Rev"
};

static std::string NormalizeSpokenNumbers(const std::string& t) {
    static const std::pair<std::string,std::string> nums[] = {
        {"one","1"},{"two","2"},{"three","3"},{"four","4"},{"five","5"},
        {"six","6"},{"seven","7"},{"eight","8"},{"nine","9"},{"ten","10"},
        {"eleven","11"},{"twelve","12"},{"thirteen","13"},{"fourteen","14"},
        {"fifteen","15"},{"sixteen","16"},{"seventeen","17"},{"eighteen","18"},
        {"nineteen","19"},{"twenty","20"},{"thirty","30"},
    };
    std::string out = t;
    for (auto& [word, digit] : nums) {
        std::regex re("\\b" + word + "\\b", std::regex::icase);
        out = std::regex_replace(out, re, digit);
    }
    return out;
}

HomeInRefParser::HomeInRefParser() {
    try {
        standard_ref_regex = std::regex(
            R"(\b((?:[123](?:st|nd|rd|th)?\s*)?[a-zA-Z]+(?:\s+of\s+[a-zA-Z]+)?)\s*)"
            R"((?:chapter\s+)?(\d+)\s*)"
            R"((?::|,|verse\s+|\s)\s*)"      // added comma + bare space
            R"((\d+)(?:\s*[-–]\s*(\d+))?\b)",
            std::regex_constants::icase
        );

        verse_only_regex = std::regex(
            R"(\b(?:verse|v|vs|vrt)\.?\s*(\d+)(?:\s*-\s*(\d+))?\b)",
            std::regex_constants::icase
        );
    } catch (const std::regex_error& e) {
        // Fallback to simple regex
        standard_ref_regex = std::regex(".*", std::regex_constants::icase);
        verse_only_regex = std::regex(".*", std::regex_constants::icase);
    }
}

HomeInRefParser::~HomeInRefParser() {}

std::vector<BibleRef> HomeInRefParser::Parse(const std::string& text) {
    std::string normalized = NormalizeSpokenNumbers(text);
    std::vector<BibleRef> results;

    // --- Layer 1: Standard references (Regex) ---
    auto it  = std::sregex_iterator(normalized.begin(), normalized.end(), standard_ref_regex);
    auto end = std::sregex_iterator();

    for (; it != end; ++it) {
        std::smatch match = *it;
        std::string book_candidate = match[1].str();
        
        // Trim
        book_candidate.erase(std::find_if(book_candidate.rbegin(), book_candidate.rend(),
            [](unsigned char ch){ return !std::isspace(ch); }).base(), book_candidate.end());

        std::string found_name;
        for (const auto& book : BIBLE_BOOKS) {
            if (iequals(book_candidate, book)) { found_name = book; break; }
        }
        if (found_name.empty()) continue;

        BibleRef ref;
        ref.original_text = match.str();
        ref.book          = found_name;
        ref.chapter       = std::stoi(match[2].str());
        ref.verse_start   = std::stoi(match[3].str());
        ref.verse_end     = match[4].matched ? std::stoi(match[4].str()) : ref.verse_start;
        ref.confidence    = 0.95f;

        // Update Context
        if (context) context->Update(ref.book, ref.chapter);

        results.push_back(ref);
    }

    // --- Layer 2: Contextual References (Implied Book/Chapter) ---
    std::string current_book;
    int current_chapter;
    if (results.empty() && context && context->GetCurrent(current_book, current_chapter)) {
        auto v_it  = std::sregex_iterator(normalized.begin(), normalized.end(), verse_only_regex);
        auto v_end = std::sregex_iterator();

        for (; v_it != v_end; ++v_it) {
            std::smatch match = *v_it;
            BibleRef ref;
            ref.original_text = match.str();
            ref.book          = current_book;
            ref.chapter       = current_chapter;
            ref.verse_start   = std::stoi(match[1].str());
            ref.verse_end     = match[2].matched ? std::stoi(match[2].str()) : ref.verse_start;
            ref.confidence    = 0.80f;
            results.push_back(ref);
        }
    }

    return results;
}
