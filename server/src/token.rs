//! Random codes (one-time links, sessions, CSRF tokens): 128 bits from the operating system, written in
//! Crockford's base32 (26 characters, no I L O U), and kept in the database only as their SHA-256.

use sha2::{Digest, Sha256};

const ALPHABET: &[u8; 32] = b"0123456789ABCDEFGHJKMNPQRSTVWXYZ";
pub const CODE_LEN: usize = 26;

pub fn random_bytes() -> [u8; 16] {
    let mut b = [0u8; 16];
    // A failure of the system random source is not something to continue after: no code is better than a weak one.
    getrandom::fill(&mut b).expect("the operating system has no random bytes");
    b
}

/// 128 bits as 26 characters (the last one carries 3 bits).
pub fn encode(bytes: &[u8; 16]) -> String {
    let mut n = u128::from_be_bytes(*bytes);
    let mut out = [0u8; CODE_LEN];
    for slot in out.iter_mut().rev() {
        *slot = ALPHABET[(n & 31) as usize];
        n >>= 5;
    }
    String::from_utf8(out.to_vec()).expect("the alphabet is ASCII")
}

pub fn new_code() -> String {
    encode(&random_bytes())
}

/// Is this the shape of a code (26 characters of the alphabet, capitals only)? Anything else is not worth a query.
pub fn well_formed(s: &str) -> bool {
    s.len() == CODE_LEN && s.bytes().all(|b| ALPHABET.contains(&b)) && s.as_bytes()[0] <= b'7'
}

/// What is stored: the SHA-256 of the code.
pub fn hash(code: &str) -> Vec<u8> {
    Sha256::digest(code.as_bytes()).to_vec()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_code_has_26_characters_of_the_alphabet() {
        for _ in 0..200 {
            let c = new_code();
            assert_eq!(c.len(), 26);
            assert!(well_formed(&c), "{c}");
        }
    }

    #[test]
    fn codes_do_not_repeat() {
        let mut seen = std::collections::HashSet::new();
        for _ in 0..1000 {
            assert!(seen.insert(new_code()));
        }
    }

    #[test]
    fn encoding_is_the_big_endian_base32_of_the_bytes() {
        assert_eq!(encode(&[0; 16]), "00000000000000000000000000");
        assert_eq!(encode(&[0xFF; 16]), "7ZZZZZZZZZZZZZZZZZZZZZZZZZ");
        let mut one = [0u8; 16];
        one[15] = 1;
        assert_eq!(encode(&one), "00000000000000000000000001");
    }

    #[test]
    fn only_the_exact_shape_passes() {
        assert!(well_formed("7ZZZZZZZZZZZZZZZZZZZZZZZZZ"));
        for bad in [
            "",
            "0",
            "0000000000000000000000000",
            "000000000000000000000000000",
            "8ZZZZZZZZZZZZZZZZZZZZZZZZZ",
            "0000000000000000000000000I",
            "0000000000000000000000000O",
            "0000000000000000000000000U",
            "000000000000000000000000a0",
            "00000000000000000000000 00",
            "../../etc/passwd/../../xx",
            "0000000000000000000000000\n",
        ] {
            assert!(!well_formed(bad), "{bad:?}");
        }
    }

    #[test]
    fn the_stored_form_is_a_sha256_of_the_code() {
        let h = hash("00000000000000000000000000");
        assert_eq!(h.len(), 32);
        assert_ne!(h, hash("00000000000000000000000001"));
        assert_eq!(h, hash("00000000000000000000000000"));
    }
}
