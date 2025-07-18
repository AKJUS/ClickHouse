#include <Functions/GatherUtils/GatherUtils.h>
#include <Functions/GatherUtils/Sinks.h>
#include <Functions/GatherUtils/Sources.h>
#include <base/TypeLists.h>

namespace DB::GatherUtils
{
/// Creates IArraySink from ColumnArray

namespace
{

template <typename... Types>
struct ArraySinkCreator;

template <typename Type, typename... Types>
struct ArraySinkCreator<Type, Types...>
{
    static std::unique_ptr<IArraySink> create(IColumn & values, ColumnArray::Offsets & offsets, size_t column_size)
    {
        using ColVecType = ColumnVectorOrDecimal<Type>;

        IColumn * not_null_values = &values;
        bool is_nullable = false;

        if (auto * nullable = typeid_cast<ColumnNullable *>(&values))
        {
            not_null_values = &nullable->getNestedColumn();
            is_nullable = true;
        }

        if (typeid_cast<ColVecType *>(not_null_values))
        {
            if (is_nullable)
                return std::make_unique<NullableArraySink<NumericArraySink<Type>>>(values, offsets, column_size);
            return std::make_unique<NumericArraySink<Type>>(values, offsets, column_size);
        }

        return ArraySinkCreator<Types...>::create(values, offsets, column_size);
    }
};

template <>
struct ArraySinkCreator<>
{
    static std::unique_ptr<IArraySink> create(IColumn & values, ColumnArray::Offsets & offsets, size_t column_size)
    {
        if (typeid_cast<ColumnNullable *>(&values))
            return std::make_unique<NullableArraySink<GenericArraySink>>(values, offsets, column_size);
        return std::make_unique<GenericArraySink>(values, offsets, column_size);
    }
};

}

std::unique_ptr<IArraySink> createArraySink(ColumnArray & col, size_t column_size)
{
    using Creator = TypeListChangeRoot<ArraySinkCreator, TypeListNumberWithUUID>;
    return Creator::create(col.getData(), col.getOffsets(), column_size);
}
}
