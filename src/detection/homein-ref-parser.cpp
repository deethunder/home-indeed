#include "homein-ref-parser.hpp"
#include <unordered_set>
#include <algorithm>
#include <cctype>

// FIX #14: _stricmp is Windows-only. Use a portable lowercase compare.
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

HomeInRefParser::HomeInRefParser() {
    standard_ref_regex = std::regex(
        R"(\b((?:[123](?:st|nd|rd|th)?\s*)?[a-zA-Z]+)\s*(?:chapter\s+)?(\d+)\s*(?::|verse\s+)\s*(\d+)(?:\s*-\s*(\d+))?\b)",
        std::regex_constants::icase
    );

    conversational_verse_regex = std::regex(
        R"(\b(?:verse|v|vs|vrt)\.?\s*(\d+)(?:\s*-\s*(\d+))?\b)",
        std::regex_constants::icase
    );
}

HomeInRefParser::~HomeInRefParser() {}

std::vector<BibleRef> HomeInRefParser::Parse(const std::string& text) {
    std::vector<BibleRef> results;

    // --- Strategy 1: Standard references (John 3:16) ---
    auto it  = std::sregex_iterator(text.begin(), text.end(), standard_ref_regex);
    auto end = std::sregex_iterator(); // universal end sentinel

    for (; it != end; ++it) {
        std::smatch match = *it;
        std::string book_candidate = match[1].str();

        // Trim trailing whitespace
        book_candidate.erase(
            std::find_if(book_candidate.rbegin(), book_candidate.rend(),
                         [](unsigned char ch){ return !std::isspace(ch); }).base(),
            book_candidate.end());

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

        last_book    = ref.book;
        last_chapter = ref.chapter;

        results.push_back(ref);
    }

    // --- Strategy 2: Contextual "verse 16" style ---
    // Only when no standard ref was found and we have a context book/chapter.
    if (results.empty() && !last_book.empty()) {
        // FIX #12: Use a fresh end iterator scoped to this regex.
        // The old code reused words_end from the standard regex loop — fragile
        // (both compare equal to a default-constructed iterator, but semantically wrong).
        auto conv_it  = std::sregex_iterator(text.begin(), text.end(), conversational_verse_regex);
        auto conv_end = std::sregex_iterator();

        for (; conv_it != conv_end; ++conv_it) {
            std::smatch match = *conv_it;
            BibleRef ref;
            ref.original_text = match.str();
            ref.book          = last_book;
            ref.chapter       = last_chapter;
            ref.verse_start   = std::stoi(match[1].str());
            ref.verse_end     = match[2].matched ? std::stoi(match[2].str()) : ref.verse_start;
            ref.confidence    = 0.85f;
            results.push_back(ref);
        }
    }

    return results;
}
