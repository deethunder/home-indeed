#include "homein-ref-parser.hpp"
#include <iostream>

HomeInRefParser::HomeInRefParser() {
    // Regex for: optional number (1, 2, 3), space, Book Name, space, Chapter, colon, Verse (with optional range)
    // Matches: "1 John 1:9", "John 3:16", "Genesis 1:1-5"
    standard_ref_regex = std::regex(R"(((?:[123]\s*)?[a-zA-Z]+)\s+(\d+)\s*:\s*(\d+)(?:\s*-\s*(\d+))?)", std::regex_constants::icase);
}

HomeInRefParser::~HomeInRefParser() {}

std::vector<BibleRef> HomeInRefParser::Parse(const std::string& text) {
    std::vector<BibleRef> results;
    
    auto words_begin = std::sregex_iterator(text.begin(), text.end(), standard_ref_regex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        std::smatch match = *i;
        BibleRef ref;
        ref.original_text = match.str();
        ref.book = match[1].str();
        ref.chapter = std::stoi(match[2].str());
        ref.verse_start = std::stoi(match[3].str());
        
        if (match[4].matched) {
            ref.verse_end = std::stoi(match[4].str());
        } else {
            ref.verse_end = ref.verse_start;
        }

        // Basic heuristic: shorter book names are less reliable
        if (ref.book.length() < 3) continue;
        
        // Confidence calculation (can be improved with bible book name dictionary matching)
        ref.confidence = 0.8f; 

        results.push_back(ref);
    }

    return results;
}
