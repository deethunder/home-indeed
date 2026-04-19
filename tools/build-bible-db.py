import os
import sqlite3
import xml.etree.ElementTree as ET
import sys

# Configuration
XML_DIR = 'vendor/beblia-xml'
DB_PATH = 'data/homein-bible.db'
TRANSLATIONS_TO_IMPORT = [
    ('EnglishKJBible.xml', 'KJV', 'King James Version'),
    ('EnglishASVBible.xml', 'ASV', 'American Standard Version'),
    ('EnglishYLTBible.xml', 'YLT', 'Young\'s Literal Translation'),
    ('EnglishDarbyBible.xml', 'DBY', 'Darby Translation'),
    ('EnglishESVBible.xml', 'ESV', 'English Standard Version') # Fallback for WEB
]

def build_db():
    print(f"Opening database at {DB_PATH}...")
    os.makedirs(os.path.dirname(DB_PATH), exist_ok=True)
    conn = sqlite3.connect(DB_PATH)
    c = conn.cursor()

    # Create tables
    c.execute('DROP TABLE IF EXISTS translations')
    c.execute('DROP TABLE IF EXISTS books')
    c.execute('DROP TABLE IF EXISTS verses')
    
    c.execute('CREATE TABLE translations (id INTEGER PRIMARY KEY, name TEXT, abbreviation TEXT)')
    c.execute('CREATE TABLE books (id INTEGER PRIMARY KEY, name TEXT, abbreviation TEXT)')
    c.execute('CREATE VIRTUAL TABLE verses USING fts5(translation_id UNINDEXED, book_id UNINDEXED, chapter UNINDEXED, verse UNINDEXED, text)')

    book_name_to_id = {}
    next_book_id = 1

    for filename, abbr, full_name in TRANSLATIONS_TO_IMPORT:
        file_path = os.path.join(XML_DIR, filename)
        if not os.path.exists(file_path):
            print(f"Warning: {file_path} not found, skipping...")
            continue
        
        print(f"Importing {full_name} ({abbr})...")
        c.execute('INSERT INTO translations (name, abbreviation) VALUES (?, ?)', (full_name, abbr))
        translation_id = c.lastrowid

        tree = ET.parse(file_path)
        root = tree.getroot()

        # Beblia structure: bible -> testament -> book -> chapter -> verse
        for testament in root.findall('testament'):
            for book in testament.findall('book'):
                book_name = book.get('name')
                if not book_name:
                    # Some files might use 'number' and we need a lookup, but Beblia usually has names
                    # If name is missing, we'll try to find it or use a default
                    book_name = f"Book {book.get('number')}"
                
                if book_name not in book_name_to_id:
                    c.execute('INSERT INTO books (name) VALUES (?)', (book_name,))
                    book_name_to_id[book_name] = c.lastrowid
                
                book_id = book_name_to_id[book_name]

                for chapter in book.findall('chapter'):
                    chapter_num = chapter.get('number')
                    for verse in chapter.findall('verse'):
                        verse_num = verse.get('number')
                        verse_text = verse.text
                        if verse_text:
                            c.execute('INSERT INTO verses (translation_id, book_id, chapter, verse, text) VALUES (?, ?, ?, ?, ?)',
                                      (translation_id, book_id, chapter_num, verse_num, verse_text))

        conn.commit()
    
    conn.close()
    print("Database build complete!")

if __name__ == "__main__":
    build_db()
