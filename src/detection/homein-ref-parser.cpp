#include "homein-ref-parser.hpp"
#include <iostream>
#include <unordered_set>
#include <algorithm>

// Static dictionary of valid Bible books (Names and Common Abbreviations)
static const std::unordered_set<std::string> BIBLE_BOOKS = {
    "Genesis", "Gen", "Exodus", "Exo", "Leviticus", "Lev", "Numbers", "Num", "Deuteronomy", "Deut",
    "Joshua", "Josh", "Judges", "Judg", "Ruth", "1 Samuel", "2 Samuel", "1 Kings", "2 Kings",
    "1 Chronicles", "2 Chronicles", "Ezra", "Nehemiah", "Esther", "Job", "Psalms", "Psa", "Proverbs", "Prov",
    "Ecclesiastes", "Ecc", "Song of Solomon", "Isaiah", "Isa", "Jeremiah", "Jer", "Lamentations", "Lam",
    "Ezekiel", "Eze", "Daniel", "Dan", "Hosea", "Hos", "Joel", "Amos", "Obadiah", "Oba", "Jonah", "Jon",
    "Micah", "Mic", "Nahum", "Nah", "Habakkuk", "Hab", "Zephaniah", "Zep", "Haggai", "Hag", "Zechariah", "Zec",
    "Malachi", "Mal", "Matthew", "Matt", "Mark", "Luke", "John", "Acts", "Romans", "Rom", "1 Corinthians", "1 Cor",
    "2 Corinthians", "2 Cor", "Galatians", "Gal", "Ephesians", "Eph", "Philippians", "Phi", "Colossians", "Col",
    "1 Thessalonians", "1 Thes", "2 Thessalonians", "2 Thes", "1 Timothy", "1 Tim", "2 Timothy", "2 Tim",
    "Titus", "Philemon", "Phi", "Hebrews", "Heb", "James", "Jas", "1 Peter", "1 Pet", "2 Peter", "2 Pet",
    "1 John", "2 John", "3 John", "Jude", "Revelation", "Rev"
};

HomeInRefParser::HomeInRefParser() {
    // Improved Regex:
    // 1. Optional number (1st, 2nd, 3, etc.)
    // 2. Book name (alphabetical characters with optional trailing dot or space)
    // 3. Chapter and Verse (support for "chapter" and "verse" as spoken words)
    // Matches: "John 3:16", "Romans chapter 12 verse 1", "1 John 1:9", "Gen 1:1"
    standard_ref_regex = std::regex(
        R"(\b((?:[123](?:st|nd|rd|th)?\s*)?[a-zA-Z]+)\s+(?:chapter\s+)?(\d+)\s*(?::|verse\s+)\s*(\d+)(?:\s*-\s*(\d+))?\b)", 
        std::regex_constants::icase
    );

    // [New] Conversational Verse Regex: matches "verse 16", "v. 16", "vs 16"
    conversational_verse_regex = std::regex(
        R"(\b(?:verse|v|vs|vrt)\.?\s*(\d+)(?:\s*-\s*(\d+))?\b)",
        std::regex_constants::icase
    );
}

HomeInRefParser::~HomeInRefParser() {}

std::vector<BibleRef> HomeInRefParser::Parse(const std::string& text) {
    std::vector<BibleRef> results;
    
    // 1. Standard Reference Parsing (John 3:16)
    auto words_begin = std::sregex_iterator(text.begin(), text.end(), standard_ref_regex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        std::string book_candidate = match[1].str();
        
        // Clean up and validate book name
        std::string clean_book = book_candidate;
        clean_book.erase(std::find_if(clean_book.rbegin(), clean_book.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), clean_book.end());

        std::string found_name;
        for (const auto& book : BIBLE_BOOKS) {
            if (_stricmp(clean_book.c_str(), book.c_str()) == 0) {
                found_name = book;
                break;
            }
        }

        if (found_name.empty()) continue;

        BibleRef ref;
        ref.original_text = match.str();
        ref.book = found_name;
        ref.chapter = std::stoi(match[2].str());
        ref.verse_start = std::stoi(match[3].str());
        ref.verse_end = match[4].matched ? std::stoi(match[4].str()) : ref.verse_start;
        ref.confidence = 0.95f; 

        // Update Context
        last_book = ref.book;
        last_chapter = ref.chapter;

        results.push_back(ref);
    }

    // 2. Intelligent Contextual Parsing ("verse 16")
    // Only if no standard references were found in this chunk
    if (results.empty() && !last_book.empty()) {
        auto conv_begin = std::sregex_iterator(text.begin(), text.end(), conversational_verse_regex);
        auto conv_end = std::sregex_iterator();
        for (std::sregex_iterator i = conv_begin; i != conv_end; ++i) {
            std::smatch match = *i;
            
            BibleRef ref;
            ref.original_text = match.str();
            ref.book = last_book;
            ref.chapter = last_chapter;
            ref.verse_start = std::stoi(match[1].str());
            ref.verse_end = match[2].matched ? std::stoi(match[2].str()) : ref.verse_start;
            
            // Lower confidence because it's contextual
            ref.confidence = 0.85f; 
            
            results.push_back(ref);
        }
    }

    return results;
}
