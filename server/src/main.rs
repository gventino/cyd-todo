#![forbid(unsafe_code)]

use cyd_todo_server::config::Config;
use cyd_todo_server::db::Db;

#[tokio::main]
async fn main() {
    let cfg = match Config::from_env() {
        Ok(c) => c,
        Err(e) => {
            eprintln!("configuration error: {e}");
            std::process::exit(2);
        }
    };
    match Db::connect(&cfg.database_url).await {
        Ok(_) => println!("database ready (no routes yet)"),
        Err(e) => {
            eprintln!("database error: {e}");
            std::process::exit(1);
        }
    }
}
