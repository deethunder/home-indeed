import os
import sqlite3
import argparse
import xml.etree.ElementTree as ET
import re

def strip_rtf(text):
    if not text or not text.startswith('{\\rtf'):
        return text
    # Very basic RTF stripper
    text = re.sub(r'\\par|\\line', '\n', text)
    text = re.sub(r'\{\*?\\.*\}', '', text)
    text = re.sub(r'\\[a-z0-9]+', '', text)
    text = re.sub(r'[{}]', '', text)
    return text.strip()

def parse_openlyrics(file_path):
    """Parses an OpenLyrics XML file and returns (title, author, content)"""
    try:
        tree = ET.parse(file_path)
        root = tree.getroot()
        ns = {'ns': 'http://openlyrics.info/namespace/2009/song'}
        
        # Get Title
        title = ""
        title_elem = root.find('.//ns:title', ns)
        if title_elem is not None:
            title = title_elem.text
            
        # Get Author
        author = ""
        author_elem = root.find('.//ns:author', ns)
        if author_elem is not None:
            author = author_elem.text

        # Get Lyrics
        lyrics = []
        for verse in root.findall('.//ns:verse', ns):
            lines = verse.find('ns:lines', ns)
            if lines is not None:
                # OpenLyrics uses <br/> for newlines inside <lines>
                # ET.tostring or itertext can help get the text
                verse_text = "".join(lines.itertext()).strip()
                lyrics.append(verse_text)
        
        return title, author, "\n\n".join(lyrics)
    except Exception as e:
        print(f"  [XML Error] Failed to parse {os.path.basename(file_path)}: {e}")
        return None, None, None

def import_folder(db_path, folder_path):
    if not os.path.exists(db_path):
        print(f"Error: Database not found at {db_path}")
        return

    conn = sqlite3.connect(db_path)
    cursor = conn.cursor()

    # Ensure schema is correct
    cursor.execute('''
        CREATE TABLE IF NOT EXISTS lyrics (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            artist TEXT,
            content TEXT NOT NULL,
            source TEXT
        )
    ''')

    count = 0
    print(f"Starting import from: {folder_path}")
    
    for filename in os.listdir(folder_path):
        path = os.path.join(folder_path, filename)
        if not os.path.isfile(path): continue
        
        title, author, content = None, "", None
        source = "BulkImport"

        if filename.endswith(".xml"):
            title, author, content = parse_openlyrics(path)
            source = "OpenLyrics"
        elif filename.endswith(".txt"):
            title = os.path.splitext(filename)[0]
            with open(path, 'r', encoding='utf-8', errors='ignore') as f:
                content = f.read()
            content = strip_rtf(content)
            source = "TextImport"

        if title and content:
            cursor.execute(
                "INSERT INTO lyrics (title, artist, content, source) VALUES (?, ?, ?, ?)",
                (title, author, content, source)
            )
            count += 1
            if count % 20 == 0:
                print(f"  - Processed {count} files...")

    conn.commit()
    
    # Rebuild FTS for fast searching
    try:
        cursor.execute("INSERT INTO lyrics_fts(lyrics_fts) VALUES('rebuild')")
        conn.commit()
        print("  - FTS Index Rebuilt.")
    except Exception as e:
        print(f"  - Note: Could not rebuild FTS index (Table might not exist yet): {e}")

    conn.close()
    print(f"\nSUCCESS: Imported {count} songs into the Home Indeed database.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Structured bulk import for Home Indeed.")
    parser.add_argument("db", help="Path to homein-lyrics.db")
    parser.add_argument("folder", help="Path to folder of .xml or .txt songs")
    
    args = parser.parse_args()
    import_folder(args.db, args.folder)

