import os
import sqlite3
import xml.etree.ElementTree as ET
import argparse

def import_openlyrics(file_path, conn):
    """Parses an OpenLyrics XML file and inserts into DB."""
    try:
        tree = ET.parse(file_path)
        root = tree.getroot()
        ns = {'ns': 'http://openlyrics.info/namespace/2009/song'}
        
        title_elem = root.find('.//ns:title', ns)
        title = title_elem.text if title_elem is not None else "Unknown Title"
        
        author_elem = root.find('.//ns:author', ns)
        artist = author_elem.text if author_elem is not None else "Various"
        
        # Collect all verse text
        verses = []
        for lines in root.findall('.//ns:lines', ns):
            verse_text = "".join(lines.itertext())
            if verse_text:
                verses.append(verse_text.strip())
        
        content = "\n\n".join(verses)
        
        c = conn.cursor()
        c.execute('INSERT INTO lyrics (title, artist, content, source) VALUES (?, ?, ?, ?)',
                  (title, artist, content, 'OpenLyrics Import'))
        return True
    except Exception as e:
        print(f"Error parsing {file_path}: {e}")
        return False

def main():
    parser = argparse.ArgumentParser(description="Import lyrics into Home Indeed database.")
    parser.add_argument("folder", help="Folder containing .xml (OpenLyrics) files")
    parser.add_argument("--db", default="data/homein-lyrics.db", help="Path to lyrics database")
    
    args = parser.parse_args()
    
    if not os.path.exists(args.folder):
        print(f"Error: Folder {args.folder} does not exist.")
        return

    conn = sqlite3.connect(args.db)
    
    count = 0
    for filename in os.listdir(args.folder):
        if filename.endswith(".xml"):
            full_path = os.path.join(args.folder, filename)
            if import_openlyrics(full_path, conn):
                count += 1
    
    conn.commit()
    conn.close()
    print(f"Successfully imported {count} songs from {args.folder}.")

if __name__ == "__main__":
    main()
