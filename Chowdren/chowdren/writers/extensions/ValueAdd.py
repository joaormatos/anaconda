from chowdren.writers.objects import ObjectWriter
from chowdren.common import (get_image_name, get_animation_name, to_c,
    make_color)
from chowdren.writers.events import (ComparisonWriter, ActionMethodWriter,
    ConditionMethodWriter, ExpressionMethodWriter, make_table)

class ValueAdd(ObjectWriter):
    class_name = 'ValueAdd'
    defines = ['CHOWDREN_USE_VALUEADD']
    static = True

    def write_init(self, writer):
        pass

hashed_keys = {}

def hash_key(value):
    value = value.lower()
    try:
        return hashed_keys[value]
    except KeyError:
        pass
    new_hash = len(hashed_keys)
    hashed_keys[value] = new_hash
    return new_hash

class SpreadValue(ActionMethodWriter):
    custom = True

    def write(self, writer):
        key = self.parameters[1].loader.items
        key = hash_key(self.converter.convert_static_expression(key))
        start = self.convert_index(2)
        step = self.convert_index(3)
        obj = self.get_object()
        object_list = self.converter.get_object(obj, True)
        writer.putlnc('spread_value(%s, %s, %s, %s);', object_list, key, start,
                      step)

    def get_object(self):
        parameter = self.parameters[0].loader
        return parameter.objectInfo, parameter.objectType

class SetAction(ActionMethodWriter):
    def write(self, writer):
        key = self.parameters[1].loader.items
        key = hash_key(self.converter.convert_static_expression(key))
        value = self.convert_index(2)
        writer.putc('get_extra_alterables().%s(%s, %s);', self.func, key,
                    value)

    def get_object(self):
        parameter = self.parameters[0].loader
        return parameter.objectInfo, parameter.objectType

class SetValue(SetAction):
    func = 'set_value'

class SetString(SetAction):
    func = 'set_string'

class CompareValue(ComparisonWriter):
    def __init__(self, *arg, **kw):
        ComparisonWriter.__init__(self, *arg, **kw)
        parameter = self.parameters[0].loader
        self.obj = (parameter.objectInfo, parameter.objectType)
        key = self.parameters[1].loader.items
        self.key = hash_key(self.converter.convert_static_expression(key))
        self.parameters = self.parameters[2:]

    def get_comparison_value(self):
        return 'get_extra_alterables().get_value(%s)' % self.key

    def get_object(self):
        return self.obj

class GetExpression(ExpressionMethodWriter):
    def get_string(self):
        converter = self.converter
        items = converter.expression_items

        last_exp = items[converter.item_index + 2]
        if last_exp.getName() != 'Virgule':
            raise NotImplementedError()
        next_exp = items[converter.item_index + 1]
        obj = (next_exp.objectInfo, next_exp.objectType)
        converter.item_index += 2

        last_exp = items[converter.item_index + 2]
        if last_exp.getName() != 'EndParenthesis':
            raise NotImplementedError()
        next_exp = items[converter.item_index + 1]
        name = hash_key(next_exp.loader.value)
        converter.item_index += 2

        obj = self.converter.get_object(obj)
        return '%s->get_extra_alterables().%s(%s)' % (obj, self.func, name)

class GetValue(GetExpression):
    func = 'get_value'

class GetString(GetExpression):
    func = 'get_string'

actions = make_table(ActionMethodWriter, {
    0 : SetValue,
    1 : SpreadValue,
    2 : SetString
})

conditions = make_table(ConditionMethodWriter, {
    0 : CompareValue
})

expressions = make_table(ExpressionMethodWriter, {
    0 : GetValue,
    1 : GetString
})

def get_object():
    return ValueAdd
