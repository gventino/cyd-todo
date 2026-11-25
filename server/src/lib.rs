//! cyd-todo server: the device API and (from A2) the phone pages of the quest log.
#![forbid(unsafe_code)]

pub mod app;
pub mod config;
pub mod db;
pub mod error;
pub mod faults;
pub mod html;
pub mod http_error;
pub mod limits;
pub mod listing;
pub mod pages;
pub mod seed;
pub mod timeutil;
pub mod token;
