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
    "1 Samuel", "2 Samuel", "1 Kings", "2 Kings", "1 King", "2 King",
    "1 Chronicles", "2 Chronicles", "1 Chronicle", "2 Chronicle",
    "Ezra", "Nehemiah", "Esther",
    "Job", "Jup", "Jb", "Psalms", "Psalm", "Psa", "Proverbs", "Prov", "Ecclesiastes", "Ecc",
    "Song of Solomon", "Song", "Isaiah", "Isa", "Jeremiah", "Jer",
    "Lamentations", "Lam", "Ezekiel", "Eze", "Daniel", "Dan",
    "Hosea", "Hos", "Joel", "Amos", "Obadiah", "Oba", "Jonah", "Jon",
    "Micah", "Mic", "Nahum", "Nah", "Habakkuk", "Hab",
    "Zephaniah", "Zep", "Haggai", "Hag", "Zechariah", "Zec",
    "Malachi", "Mal", "Matthew", "Matt", "Mark", "Luke", "John",
    "Acts", "Romans", "Rom", "1 Corinthians", "2 Corinthians", "1 Cor", "2 Cor",
    "Galatians", "Gal", "Ephesians", "Eph",
    "Philippians", "Phil", "Colossians", "Col",
    "1 Thessalonians", "2 Thessalonians", "1 Thes", "2 Thes",
    "1 Timothy", "2 Timothy", "1 Tim", "2 Tim",
    "Titus", "Philemon", "Phlm", "Hebrews", "Heb", "James", "Jas",
    "1 Peter", "2 Peter", "1 Pet", "2 Pet",
    "1 John", "2 John", "3 John", "Jude", "Revelation", "Revelations", "Rev"
};

static std::string NormalizeSpokenNumbers(const std::string& t) {
    std::string out = t;

    // 1. Tag "hundreds" so we can collapse them mathematically later.
    // Maps "one hundred and" or "a hundred" to a temporary "100and " tag.
    static const std::regex re_hund_and("\\b(?:one|a)\\s+hundred\\s+and\\b", std::regex::icase);
    static const std::regex re_hund("\\b(?:one|a)\\s+hundred\\b", std::regex::icase);
    out = std::regex_replace(out, re_hund_and, "100and ");
    out = std::regex_replace(out, re_hund, "100and ");

    // 2. Compile 1-99 regexes ONLY ONCE for a massive CPU performance gain
    static const std::vector<std::pair<std::regex, std::string>> num_regexes = []() {
        std::vector<std::pair<std::regex, std::string>> res;
        const std::pair<std::string, std::string> nums[] = {
            // 20-99 compounds
            {"twenty one","21"},{"twenty two","22"},{"twenty three","23"},{"twenty four","24"},{"twenty five","25"},{"twenty six","26"},{"twenty seven","27"},{"twenty eight","28"},{"twenty nine","29"},
            {"thirty one","31"},{"thirty two","32"},{"thirty three","33"},{"thirty four","34"},{"thirty five","35"},{"thirty six","36"},{"thirty seven","37"},{"thirty eight","38"},{"thirty nine","39"},
            {"forty one","41"},{"forty two","42"},{"forty three","43"},{"forty four","44"},{"forty five","45"},{"forty six","46"},{"forty seven","47"},{"forty eight","48"},{"forty nine","49"},
            {"fifty one","51"},{"fifty two","52"},{"fifty three","53"},{"fifty four","54"},{"fifty five","55"},{"fifty six","56"},{"fifty seven","57"},{"fifty eight","58"},{"fifty nine","59"},
            {"sixty one","61"},{"sixty two","62"},{"sixty three","63"},{"sixty four","64"},{"sixty five","65"},{"sixty six","66"},{"sixty seven","67"},{"sixty eight","68"},{"sixty nine","69"},
            {"seventy one","71"},{"seventy two","72"},{"seventy three","73"},{"seventy four","74"},{"seventy five","75"},{"seventy six","76"},{"seventy seven","77"},{"seventy eight","78"},{"seventy nine","79"},
            {"eighty one","81"},{"eighty two","82"},{"eighty three","83"},{"eighty four","84"},{"eighty five","85"},{"eighty six","86"},{"eighty seven","87"},{"eighty eight","88"},{"eighty nine","89"},
            {"ninety one","91"},{"ninety two","92"},{"ninety three","93"},{"ninety four","94"},{"ninety five","95"},{"ninety six","96"},{"ninety seven","97"},{"ninety eight","98"},{"ninety nine","99"},
            // Tens
            {"twenty","20"},{"thirty","30"},{"forty","40"},{"fifty","50"},{"sixty","60"},{"seventy","70"},{"eighty","80"},{"ninety","90"},
            // Ordinals for Book Names
            {"first","1"},{"second","2"},{"third","3"},
            // Teens and singles
            {"eleven","11"},{"twelve","12"},{"thirteen","13"},{"fourteen","14"},{"fifteen","15"},{"sixteen","16"},{"seventeen","17"},{"eighteen","18"},{"nineteen","19"},{"ten","10"},
            {"one","1"},{"two","2"},{"three","3"},{"four","4"},{"five","5"},{"six","6"},{"seven","7"},{"eight","8"},{"nine","9"}
        };
        for (const auto& p : nums) {
            res.push_back({std::regex("\\b" + p.first + "\\b", std::regex::icase), p.second});
        }
        return res;
    }();

    // Apply the 1-99 mappings
    for (const auto& p : num_regexes) {
        out = std::regex_replace(out, p.first, p.second);
    }

    // 3. Collapse the "hundreds" tags with the newly parsed digits
    // e.g., "100and 76" -> "176"
    static const std::regex re_h2("\\b100and\\s+(\\d{2})\\b", std::regex::icase);
    out = std::regex_replace(out, re_h2, "1$1");
    
    // e.g., "100and 5" -> "105"
    static const std::regex re_h1("\\b100and\\s+(\\d{1})\\b", std::regex::icase);
    out = std::regex_replace(out, re_h1, "10$1");
    
    // Leftovers (e.g., "100and verse 5" -> "100 verse 5")
    static const std::regex re_h0("\\b100and\\b", std::regex::icase);
    out = std::regex_replace(out, re_h0, "100");

    return out;
}
HomeInRefParser::HomeInRefParser() {
    try {
        standard_ref_regex = std::regex(
            R"(\b((?:[123](?:st|nd|rd|th)?\s*)?[a-zA-Z]+(?:\s+of\s+[a-zA-Z]+)?)\s*)"
            R"((?:chapter\s+)?(\d+)\s*)"
            R"((?:\s*(?:from\s+)?verse\s+|\s*:\s*|\s*\.\s*|\s*,\s*|\s+))"
            R"((\d+)(?:\s*(?:[-–]|through|to|and)\s*(\d+))?\b)",         
            std::regex_constants::icase
        );

        verse_only_regex = std::regex(
            R"(\b(?:chapter\s+(\d+)\s+)?(?:from\s+)?(?:verse|v|vs|vrt|this\s+is|and\s+this\s+is|verses|section)\.?\s*(\d+)(?:\s*(?:-|–|to|through|and)\s*(\d+))?\b)",
            std::regex_constants::icase
        );
    } catch (const std::regex_error&) {
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
            
            // If they said "Chapter 2 verse 30", update the context chapter!
            if (match[1].matched) {
                ref.chapter = std::stoi(match[1].str());
                context->Update(current_book, ref.chapter); 
            } else {
                ref.chapter = current_chapter; // Use previously known chapter
            }

            ref.verse_start   = std::stoi(match[2].str());
            ref.verse_end     = match[3].matched ? std::stoi(match[3].str()) : ref.verse_start;
            ref.confidence    = 0.80f;
            results.push_back(ref);
        }
    }
    return results;
}
