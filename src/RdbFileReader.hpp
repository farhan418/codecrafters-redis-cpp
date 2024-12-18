#ifndef RDBFILEREADER_HPP
#define RDBFILEREADER_HPP

#include <iostream>
#include <fstream>
#include <string>
#include <cstdint>
#include <chrono>
#include <ctime>

#include "RedisDataStore.hpp"
#include "RedisCommandCenter.hpp"
#include "utility.hpp"


enum class ValueType : uint8_t {
    StringEncoding = 0,
    ListEncoding = 1,
    SetEncoding = 2
};

class RdbFileReader {
public:
    RdbFileReader() {
        reset();
    }

    int readFile(const std::string& filename) {
        reset();
        this->filename = filename;
        rdb_file.open(this->filename, std::ios::binary);
        if (!rdb_file.is_open()) {
            DEBUG_LOG("something went wrong, cannot open file : " + filename + "\n");
            return 1;
        }
        DEBUG_LOG("Reading file : " + filename + "\n");
        if (0 != read_header_and_metadata())
            return 1;

        if (0 != read_database())
            return 1;

        rdb_file.close();
        return 0;
    }

    ~RdbFileReader() {
        if (rdb_file.is_open()) {
            rdb_file.close();
        }
    }
private:
    // struct File {
    // public :
    //     File(const std::string& filename, const std::string) {}
    //     ~File() {}
    // private:
    // }
    int reset() {
        cursor_index = 0;
        filename = "";
        if (rdb_file.is_open()) {
            rdb_file.close();
        }
        return 0;
    }

    int read_header_and_metadata() {
        uint8_t byte;
        std::string version;
        // std::stringstream ss;
        while(peek_next_byte() != 0xFA) {
            byte = read_byte();
            version += byte;
            // ss.str("");
            // ss << "byte = " << byte << ", version = " << version << ", cursor_index = " << cursor_index; 
            // DEBUG_LOG(ss.str());
        }

        // std::string version(value);
        if (0 != version.find("REDIS")) {
            DEBUG_LOG("This file does not follow redis protocol or is not a rdb file, filename : " + filename);
            return 1;
        }
        DEBUG_LOG("Redis version : " + version);
        DEBUG_LOG("Reading metadata (string encoded key-value pairs): ");
        
        std::string key; 
        std::string value; 
        while(peek_next_byte() == 0xFA) {
            read_byte();  // read 0xFA
            // read_key_value_pair(key, value);
            key = read_length_encoded_string();
            value = read_length_encoded_string();
            DEBUG_LOG("Key : " + key + ", Value : " + value);
        }
        DEBUG_LOG("exiting read_header_and_metadata()...");
        return 0;
    }

    int read_database() {
        if (0xFE != read_byte()) {
            DEBUG_LOG("Database section should be here.\n");
            return 1;
        }

        uint32_t database_index = read_size_encoded_number();
        DEBUG_LOG("database_index = " + std::to_string(database_index));

        if (0xFB != read_byte()) {
            DEBUG_LOG("Hash table size information section should be here.\n");
            return 1;
        }

        std::string key;
        std::string value;
        uint32_t count_ht_total = 0;
        uint32_t count_ht_with_expiry = 0;

        uint32_t size_hash_table_total = read_size_encoded_number();
        uint32_t size_hash_table_with_expiry = read_size_encoded_number();

        std::stringstream ss;  
        ss << "size_hash_table_total = " << size_hash_table_total << ", size_hash_table_with_expiry = " << size_hash_table_with_expiry;
        DEBUG_LOG(ss.str());

        for(uint32_t i = 0; i < size_hash_table_total; i++) {
            uint8_t byte = peek_next_byte();
            uint64_t expiry_time_ms = UINT64_MAX;

            count_ht_total++;
            if (byte == 0xFC) {
                read_byte();  // read 0xFC
                expiry_time_ms = read_little_endian_number(8);
                count_ht_with_expiry++;
            }
            else if (byte == 0xFD) {
                read_byte();  // read 0xFD
                expiry_time_ms = read_little_endian_number(4);
                count_ht_with_expiry++;
            }
            else if (byte == static_cast<uint8_t>(ValueType::StringEncoding)) {}
            else {
                throw std::runtime_error("\nNot supported valueType for value in key, value pair\n");
            }
            read_key_value_pair(key, value);
            std::stringstream ss;  
            ss << "key : " << key << ", val : " << value << ", expiry : " << expiry_time_ms;
            DEBUG_LOG(ss.str());
            redis_data_store_obj.set_kv(key, value, expiry_time_ms);
        }

        if (count_ht_total == size_hash_table_total)
            DEBUG_LOG("read all keys from database file");

        if (peek_next_byte() == 0xff)
            DEBUG_LOG("reached end of rdb file.");

        return 0;
    }

    int read_key_value_pair(std::string& key, std::string& value) {
        uint8_t byte = read_byte();
        // std::stringstream ss;  
        // ss << "byte = " << byte;
        // if (byte == 0) DEBUG_LOG("byte is 0"); 
        // DEBUG_LOG(ss.str());
        switch(byte) {
            case static_cast<uint8_t>(ValueType::StringEncoding) :  // value is String encoded
            // DEBUG_LOG("reading string encoded kv pair");
            key = read_length_encoded_string();
            value = read_length_encoded_string();
            break;

            default :
            DEBUG_LOG("Currently only supporting ValueType = StringEncoding\n");
            return 1;
        }
        return 0;
    }

    uint8_t peek_next_byte() {
        std::streampos currentpos = rdb_file.tellg();
        uint8_t byte = read_byte();
        cursor_index -= 1;
        rdb_file.seekg(currentpos);
        // std::stringstream ss;  
        // ss << "byte read = \'" << byte << "\'" << ", cursor_index = " << cursor_index;
        // DEBUG_LOG(ss.str());
        return byte;
    }

    uint8_t read_byte() {
        uint8_t byte;
        rdb_file.read(reinterpret_cast<char*>(&byte), 1);
        if(rdb_file.gcount() != 1) {
            DEBUG_LOG("Error reading data from " + filename);
            return 1;
        }
        cursor_index += 1;
        return byte;
    }

    std::string read_length_encoded_string() {
        std::string str;
        uint8_t byte = peek_next_byte();
        uint8_t msb = byte >> 6;
        if (msb != 3) { // msb is 0 or 1 or 2
            uint32_t length = read_size_encoded_number();
            for(uint32_t i = 0; i < length; i++) {
                str += static_cast<char>(read_byte());
            }
        }
        else {  // msb = 3
            byte = read_byte() & 0x3F;  // last 6 bits
             if (byte == 0) {
                str = std::to_string(read_little_endian_number(1));
            }
            else if (byte == 1) {
                str = std::to_string(read_little_endian_number(2));
            }
            else if (byte == 2) {
                str = std::to_string(read_little_endian_number(4));
            }
            else if (byte == 3) {  // LZF compression
                throw std::runtime_error("\nInvalid, compression is not expected in this project.");
            }
        }
        // std::stringstream ss;  
        // ss << "Parsed string, length = " << str.length() << ", str = " << str;
        // DEBUG_LOG(ss.str());
        return str;
    }

    uint32_t read_size_encoded_number() {
        uint8_t byte = read_byte();
        uint8_t msb = byte >> 6;
        uint32_t length = 0;

        if (msb == 0x00) {
            length = byte & 0x3F;  // last 6 bits
        }
        else if (msb == 0x01) {
            length = byte & 0x3F;
            length = length << 8;
            length = length | read_byte();  // 14 bits
        }
        else if (msb == 0x10) {
            for (int i = 0; i < 4; i++) {
                length = length << 8;
                length = length | read_byte();
            }
        }
        else if (msb == 0x11) {
            std::runtime_error("\nNot a number, but string is stored.");
        }
        // std::stringstream ss;  
        // ss << "length = " << length;
        // DEBUG_LOG(ss.str());
        return length;
    }

    uint64_t read_little_endian_number(int num_bytes) {
        uint64_t number = 0;  // reads max 8 bytes number
        for(int i = 0; i < num_bytes; i++) {
            uint64_t temp = read_byte();
            temp = temp << (i * 8);
            number = number | temp;
        }
        // std::stringstream ss;  
        // ss << "num_bytes = " << num_bytes << ", number = " << number;
        // DEBUG_LOG(ss.str());
        return number;
    }

    size_t cursor_index;
    std::string filename;
    std::ifstream rdb_file;
    RedisDataStore redis_data_store_obj;
};

#endif  // RDBFILEREADER_HPP