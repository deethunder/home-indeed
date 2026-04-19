import sqlite3

def verify():
    db_path = 'data/homein-bible.db'
    conn = sqlite3.connect(db_path)
    c = conn.cursor()
    
    c.execute('SELECT COUNT(*) FROM verses')
    count = c.fetchone()[0]
    print(f"Total verses across all translations: {count}")
    
    # Test FTS5 search
    c.execute("SELECT text FROM verses WHERE text MATCH 'beginning' LIMIT 1")
    result = c.fetchone()
    print(f"FTS5 Search Result: {result}")
    
    # Check translations
    c.execute("SELECT abbreviation, name FROM translations")
    translations = c.fetchall()
    print("Found translations:", translations)
    
    conn.close()

if __name__ == "__main__":
    verify()
