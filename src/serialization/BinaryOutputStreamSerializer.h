// Copyright (c) 2011-2015 The Cryptonote developers
// Copyright (c) 2014-2015 XDN developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "ISerializer.h"
#include "SerializationOverloads.h"

#include <ostream>

namespace cryptonote {

class BinaryOutputStreamSerializer : public ISerializer {
public:
  BinaryOutputStreamSerializer(std::ostream& strm) : stream(strm) {}
  virtual ~BinaryOutputStreamSerializer() {}

  virtual ISerializer::SerializerType type() const;

  virtual ISerializer& beginObject(const std::string& name) override;
  virtual ISerializer& endObject() override;

  virtual ISerializer& beginArray(std::size_t& size, const std::string& name) override;
  virtual ISerializer& endArray() override;

  virtual ISerializer& operator()(uint8_t& value, const std::string& name) override;
  virtual ISerializer& operator()(int32_t& value, const std::string& name) override;
  virtual ISerializer& operator()(uint32_t& value, const std::string& name) override;
  virtual ISerializer& operator()(int64_t& value, const std::string& name) override;
  virtual ISerializer& operator()(uint64_t& value, const std::string& name) override;
  virtual ISerializer& operator()(double& value, const std::string& name) override;
  virtual ISerializer& operator()(bool& value, const std::string& name) override;
  virtual ISerializer& operator()(std::string& value, const std::string& name) override;

  virtual ISerializer& binary(void* value, std::size_t size, const std::string& name) override;
  virtual ISerializer& binary(std::string& value, const std::string& name) override;

  virtual bool hasObject(const std::string& name) override;

  template<typename T>
  ISerializer& operator()(T& value, const std::string& name) {
    return ISerializer::operator()(value, name);
  }

private:
  void checkedWrite(const char* buf, size_t size);
  std::ostream& stream;
};

}
