//------------------------------------------------------------------------------
// Copyright 2018 H2O.ai
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//------------------------------------------------------------------------------
#ifndef dt_MODELS_COLUMN_HASHER_h
#define dt_MODELS_COLUMN_HASHER_h
#include "column.h"
#include "models/murmurhash.h"


/*
* An abstract base class for all the hashers.
*/
class Hasher {
  public:
    explicit Hasher(const Column*);
    virtual ~Hasher();

    const RowIndex& ri;
    virtual uint64_t hash(size_t row) const = 0;
};


using hasherptr = std::unique_ptr<Hasher>;


/*
* Class to hash booleans.
*/
class HasherBool : public Hasher {
  private:
    const int8_t* values;
  public:
    explicit HasherBool(const Column*);
    uint64_t hash(size_t row) const override;
};


/*
* Template class to hash integers.
*/
template <typename T>
class HasherInt : public Hasher {
  private:
    const T* values;
  public:
    explicit HasherInt(const Column*);
    uint64_t hash(size_t row) const override;
};


/*
*  Template class to hash floats.
*/
template <typename T>
class HasherFloat : public Hasher {
  private:
    const T* values;
  public:
    explicit HasherFloat(const Column*);
    uint64_t hash(size_t row) const override;
};


/*
*  Template class to hash strings.
*/
template <typename T>
class HasherString : public Hasher {
  private:
    const char* strdata;
    const T* offsets;
  public:
    explicit HasherString(const Column*);
    uint64_t hash(size_t row) const override;
};


extern template class HasherInt<int8_t>;
extern template class HasherInt<int16_t>;
extern template class HasherInt<int32_t>;
extern template class HasherInt<int64_t>;
extern template class HasherFloat<float>;
extern template class HasherFloat<double>;
extern template class HasherString<uint32_t>;
extern template class HasherString<uint64_t>;

#endif
