import sqlite3
import os

def init_db():
    db_path = 'data/homein-lyrics.db'
    os.makedirs(os.path.dirname(db_path), exist_ok=True)
    
    conn = sqlite3.connect(db_path)
    c = conn.cursor()
    
    # Create main table
    c.execute('''
        CREATE TABLE IF NOT EXISTS lyrics (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT,
            artist TEXT,
            content TEXT,
            source TEXT,
            last_updated TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        )
    ''')
    
    # Create FTS5 virtual table for searching
    # We use an external content table to keep the original data separated
    c.execute('''
        CREATE VIRTUAL TABLE IF NOT EXISTS lyrics_fts USING fts5(
            title, 
            artist, 
            content, 
            content='lyrics', 
            content_rowid='id'
        )
    ''')
    
    # Trigger to keep FTS index synchronized (v1.0 simple triggers)
    c.execute('''
        CREATE TRIGGER IF NOT EXISTS lyrics_ai AFTER INSERT ON lyrics BEGIN
            INSERT INTO lyrics_fts(rowid, title, artist, content) VALUES (new.id, new.title, new.artist, new.content);
        END;
    ''')
    
    conn.commit()
    conn.close()
    print("Lyrics database initialized successfully.")

if __name__ == "__main__":
    init_db()
