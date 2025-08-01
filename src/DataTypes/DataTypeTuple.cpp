#include <base/range.h>
#include <Common/StringUtils.h>
#include <Columns/ColumnTuple.h>
#include <Columns/ColumnConst.h>
#include <Core/Field.h>
#include <DataTypes/DataTypeTuple.h>
#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeFactory.h>
#include <Common/SipHash.h>
#include <DataTypes/Serializations/SerializationInfo.h>
#include <DataTypes/Serializations/SerializationTuple.h>
#include <DataTypes/Serializations/SerializationNamed.h>
#include <DataTypes/Serializations/SerializationInfoTuple.h>
#include <DataTypes/Serializations/SerializationWrapper.h>
#include <DataTypes/NestedUtils.h>
#include <Parsers/IAST.h>
#include <Parsers/ASTNameTypePair.h>
#include <Common/assert_cast.h>
#include <Common/quoteString.h>
#include <IO/WriteHelpers.h>
#include <IO/WriteBufferFromString.h>
#include <IO/Operators.h>
#include <boost/algorithm/string.hpp>

#include <ranges>

namespace DB
{

namespace ErrorCodes
{
    extern const int BAD_ARGUMENTS;
    extern const int DUPLICATE_COLUMN;
    extern const int NOT_FOUND_COLUMN_IN_BLOCK;
    extern const int NUMBER_OF_ARGUMENTS_DOESNT_MATCH;
    extern const int SIZES_OF_COLUMNS_IN_TUPLE_DOESNT_MATCH;
    extern const int ARGUMENT_OUT_OF_BOUND;
    extern const int LOGICAL_ERROR;
}


DataTypeTuple::DataTypeTuple(const DataTypes & elems_)
    : elems(elems_), has_explicit_names(false)
{
    /// Automatically assigned names in form of '1', '2', ...
    size_t size = elems.size();
    names.resize(size);
    for (size_t i = 0; i < size; ++i)
        names[i] = toString(i + 1);
}

static std::optional<Exception> checkTupleNames(const Strings & names)
{
    std::unordered_set<String> names_set;
    for (const auto & name : names)
    {
        if (name.empty())
            return Exception(ErrorCodes::BAD_ARGUMENTS, "Names of tuple elements cannot be empty");

        if (!names_set.insert(name).second)
            return Exception(ErrorCodes::DUPLICATE_COLUMN, "Names of tuple elements must be unique. Duplicate name: {}", name);
    }

    return {};
}

DataTypeTuple::DataTypeTuple(const DataTypes & elems_, const Strings & names_)
    : elems(elems_), names(names_), has_explicit_names(true)
{
    size_t size = elems.size();
    if (names.size() != size)
        throw Exception(ErrorCodes::NUMBER_OF_ARGUMENTS_DOESNT_MATCH, "Wrong number of names passed to constructor of DataTypeTuple");

    if (auto exception = checkTupleNames(names))
        throw std::move(*exception);
}

std::string DataTypeTuple::doGetName() const
{
    size_t size = elems.size();
    WriteBufferFromOwnString s;

    s << "Tuple(";
    for (size_t i = 0; i < size; ++i)
    {
        if (i != 0)
            s << ", ";

        if (has_explicit_names)
            s << backQuoteIfNeed(names[i]) << ' ';

        s << elems[i]->getName();
    }
    s << ")";

    return s.str();
}

std::string DataTypeTuple::doGetPrettyName(size_t indent) const
{
    size_t size = elems.size();
    WriteBufferFromOwnString s;

    /// If the Tuple is named, we will output it in multiple lines with indentation.
    if (has_explicit_names)
    {
        s << "Tuple(\n";

        for (size_t i = 0; i != size; ++i)
        {
            if (i != 0)
                s << ",\n";

            s << fourSpaceIndent(indent + 1)
                << backQuoteIfNeed(names[i]) << ' '
                << elems[i]->getPrettyName(indent + 1);
        }

        s << ')';
    }
    else
    {
        s << "Tuple(";

        for (size_t i = 0; i != size; ++i)
        {
            if (i != 0)
                s << ", ";
            s << elems[i]->getPrettyName(indent);
        }

        s << ')';
    }

    return s.str();
}

DataTypePtr DataTypeTuple::getNormalizedType() const
{
    DataTypes normalized_elems;
    normalized_elems.reserve(elems.size());
    for (const auto & elem : elems)
        normalized_elems.emplace_back(elem->getNormalizedType());
    return std::make_shared<DataTypeTuple>(normalized_elems);
}

static inline IColumn & extractElementColumn(IColumn & column, size_t idx)
{
    return assert_cast<ColumnTuple &>(column).getColumn(idx);
}

template <typename F>
static void addElementSafe(const DataTypes & elems, IColumn & column, F && impl)
{
    /// We use the assumption that tuples of zero size do not exist.
    size_t old_size = column.size();

    try
    {
        impl();

        // Check that all columns now have the same size.
        size_t new_size = column.size();

        for (auto i : collections::range(0, elems.size()))
        {
            const auto & element_column = extractElementColumn(column, i);
            if (element_column.size() != new_size)
            {
                // This is not a logical error because it may work with
                // user-supplied data.
                throw Exception(ErrorCodes::SIZES_OF_COLUMNS_IN_TUPLE_DOESNT_MATCH,
                    "Cannot read a tuple because not all elements are present");
            }
        }
    }
    catch (...)
    {
        for (const auto & i : collections::range(0, elems.size()))
        {
            auto & element_column = extractElementColumn(column, i);

            if (element_column.size() > old_size)
                element_column.popBack(1);
        }

        throw;
    }
}

MutableColumnPtr DataTypeTuple::createColumn() const
{
    if (elems.empty())
        return ColumnTuple::create(0);

    size_t size = elems.size();
    MutableColumns tuple_columns(size);
    for (size_t i = 0; i < size; ++i)
        tuple_columns[i] = elems[i]->createColumn();
    return ColumnTuple::create(std::move(tuple_columns));
}

MutableColumnPtr DataTypeTuple::createColumn(const ISerialization & serialization) const
{
    /// If we read subcolumn of nested Tuple or this Tuple is a subcolumn, it may be wrapped to SerializationWrapper
    /// several times to allow to reconstruct the substream path name.
    /// Here we don't need substream path name, so we drop first several wrapper serializations.
    const auto * current_serialization = &serialization;
    while (const auto * serialization_wrapper = dynamic_cast<const SerializationWrapper *>(current_serialization))
        current_serialization = serialization_wrapper->getNested().get();

    const auto * serialization_tuple = typeid_cast<const SerializationTuple *>(current_serialization);
    if (!serialization_tuple)
        throw Exception(ErrorCodes::LOGICAL_ERROR, "Unexpected serialization to create column of type Tuple");

    if (elems.empty())
        return IDataType::createColumn(serialization);

    const auto & element_serializations = serialization_tuple->getElementsSerializations();

    size_t size = elems.size();
    assert(element_serializations.size() == size);
    MutableColumns tuple_columns(size);
    for (size_t i = 0; i < size; ++i)
        tuple_columns[i] = elems[i]->createColumn(*element_serializations[i]->getNested());

    return ColumnTuple::create(std::move(tuple_columns));
}

Field DataTypeTuple::getDefault() const
{
    return Tuple(std::from_range_t{}, elems | std::views::transform([](const DataTypePtr & elem) { return elem->getDefault(); }));
}

void DataTypeTuple::insertDefaultInto(IColumn & column) const
{
    if (elems.empty())
    {
        column.insertDefault();
        return;
    }

    addElementSafe(elems, column, [&]
    {
        for (const auto & i : collections::range(0, elems.size()))
            elems[i]->insertDefaultInto(extractElementColumn(column, i));
    });
}

bool DataTypeTuple::equals(const IDataType & rhs) const
{
    if (typeid(rhs) != typeid(*this))
        return false;

    const DataTypeTuple & rhs_tuple = static_cast<const DataTypeTuple &>(rhs);

    size_t size = elems.size();
    if (size != rhs_tuple.elems.size())
        return false;

    for (size_t i = 0; i < size; ++i)
        if (!elems[i]->equals(*rhs_tuple.elems[i]) || names[i] != rhs_tuple.names[i])
            return false;

    return true;
}


size_t DataTypeTuple::getPositionByName(const String & name, bool case_insensitive) const
{
    for (size_t i = 0; i < elems.size(); ++i)
    {
        if (case_insensitive)
        {
            if (boost::iequals(names[i], name))
                return i;
        }
        else
        {
            if (boost::equals(names[i], name))
                return i;
        }
    }
    throw Exception(ErrorCodes::NOT_FOUND_COLUMN_IN_BLOCK, "Tuple doesn't have element with name '{}'", name);
}

std::optional<size_t> DataTypeTuple::tryGetPositionByName(const String & name, bool case_insensitive) const
{
    for (size_t i = 0; i < elems.size(); ++i)
    {
        if (case_insensitive)
        {
            if (boost::iequals(names[i], name))
                return i;
        }
        else
        {
            if (boost::equals(names[i], name))
                return i;
        }
    }
    return std::nullopt;
}

String DataTypeTuple::getNameByPosition(size_t i) const
{
    if (i == 0 || i > names.size())
        throw Exception(ErrorCodes::ARGUMENT_OUT_OF_BOUND, "Index of tuple element ({}) is out range ([1, {}])", i, names.size());

    return names[i - 1];
}


bool DataTypeTuple::textCanContainOnlyValidUTF8() const
{
    return std::all_of(elems.begin(), elems.end(), [](auto && elem) { return elem->textCanContainOnlyValidUTF8(); });
}

bool DataTypeTuple::haveMaximumSizeOfValue() const
{
    return std::all_of(elems.begin(), elems.end(), [](auto && elem) { return elem->haveMaximumSizeOfValue(); });
}

bool DataTypeTuple::hasDynamicSubcolumnsDeprecated() const
{
    return std::any_of(elems.begin(), elems.end(), [](auto && elem) { return elem->hasDynamicSubcolumnsDeprecated(); });
}

bool DataTypeTuple::isComparable() const
{
    return std::all_of(elems.begin(), elems.end(), [](auto && elem) { return elem->isComparable(); });
}

size_t DataTypeTuple::getMaximumSizeOfValueInMemory() const
{
    size_t res = 0;
    for (const auto & elem : elems)
        res += elem->getMaximumSizeOfValueInMemory();
    return res;
}

size_t DataTypeTuple::getSizeOfValueInMemory() const
{
    size_t res = 0;
    for (const auto & elem : elems)
        res += elem->getSizeOfValueInMemory();
    return res;
}

SerializationPtr DataTypeTuple::doGetDefaultSerialization() const
{
    SerializationTuple::ElementSerializations serializations(elems.size());

    for (size_t i = 0; i < elems.size(); ++i)
    {
        String elem_name = has_explicit_names ? names[i] : toString(i + 1);
        auto serialization = elems[i]->getDefaultSerialization();
        serializations[i] = std::make_shared<SerializationNamed>(serialization, elem_name, SubstreamType::TupleElement);
    }

    return std::make_shared<SerializationTuple>(std::move(serializations), has_explicit_names);
}

SerializationPtr DataTypeTuple::getSerialization(const SerializationInfo & info) const
{
    SerializationTuple::ElementSerializations serializations(elems.size());
    const auto & info_tuple = assert_cast<const SerializationInfoTuple &>(info);

    for (size_t i = 0; i < elems.size(); ++i)
    {
        String elem_name = has_explicit_names ? names[i] : toString(i + 1);
        auto serialization = elems[i]->getSerialization(*info_tuple.getElementInfo(i));
        serializations[i] = std::make_shared<SerializationNamed>(serialization, elem_name, SubstreamType::TupleElement);
    }

    return std::make_shared<SerializationTuple>(std::move(serializations), has_explicit_names);
}

MutableSerializationInfoPtr DataTypeTuple::createSerializationInfo(const SerializationInfoSettings & settings) const
{
    MutableSerializationInfos infos;
    infos.reserve(elems.size());
    for (const auto & elem : elems)
        infos.push_back(elem->createSerializationInfo(settings));

    return std::make_shared<SerializationInfoTuple>(std::move(infos), names);
}

SerializationInfoPtr DataTypeTuple::getSerializationInfo(const IColumn & column) const
{
    if (const auto * column_const = checkAndGetColumn<ColumnConst>(&column))
        return getSerializationInfo(column_const->getDataColumn());

    MutableSerializationInfos infos;
    infos.reserve(elems.size());

    const auto & column_tuple = assert_cast<const ColumnTuple &>(column);
    assert(elems.size() == column_tuple.getColumns().size());

    for (size_t i = 0; i < elems.size(); ++i)
    {
        auto element_info = elems[i]->getSerializationInfo(column_tuple.getColumn(i));
        infos.push_back(const_pointer_cast<SerializationInfo>(element_info));
    }

    return std::make_shared<SerializationInfoTuple>(std::move(infos), names);
}

void DataTypeTuple::forEachChild(const ChildCallback & callback) const
{
    for (const auto & elem : elems)
    {
        callback(*elem);
        elem->forEachChild(callback);
    }
}

void DataTypeTuple::updateHashImpl(SipHash & hash) const
{
    hash.update(elems.size());
    for (const auto & elem : elems)
        elem->updateHash(hash);

    hash.update(has_explicit_names);
    // Include names in the hash if they are explicitly set
    if (has_explicit_names)
    {
        hash.update(names.size());
        for (const auto & name : names)
            hash.update(name);
    }
}

static DataTypePtr create(const ASTPtr & arguments)
{
    if (!arguments || arguments->children.empty())
        return std::make_shared<DataTypeTuple>(DataTypes{});

    DataTypes nested_types;
    nested_types.reserve(arguments->children.size());

    Strings names;
    names.reserve(arguments->children.size());

    for (const ASTPtr & child : arguments->children)
    {
        if (const auto * name_and_type_pair = child->as<ASTNameTypePair>())
        {
            nested_types.emplace_back(DataTypeFactory::instance().get(name_and_type_pair->type));
            names.emplace_back(name_and_type_pair->name);
        }
        else
            nested_types.emplace_back(DataTypeFactory::instance().get(child));
    }

    if (names.empty())
        return std::make_shared<DataTypeTuple>(nested_types);
    if (names.size() != nested_types.size())
        throw Exception(ErrorCodes::BAD_ARGUMENTS, "Names are specified not for all elements of Tuple type");
    return std::make_shared<DataTypeTuple>(nested_types, names);
}


void registerDataTypeTuple(DataTypeFactory & factory)
{
    factory.registerDataType("Tuple", create);
}

}
