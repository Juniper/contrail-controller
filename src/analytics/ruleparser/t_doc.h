/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef T_DOC_H
#define T_DOC_H

/**
 * Documentation stubs
 *
 */
class t_doc {

 public:
  t_doc() : has_doc_(false) {}
  virtual ~t_doc() {}

  void set_doc(const std::string& doc) {
    doc_ = doc;
    has_doc_ = true;
  }

  const std::string& get_doc() const {
    return doc_;
  }

  bool has_doc() {
    return has_doc_;
  }

 private:
  std::string doc_;
  bool has_doc_;

};

#endif
