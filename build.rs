fn main() {
    #[cfg(feature = "esp-idf")]
    embuild::espidf::sysenv::output();
}
