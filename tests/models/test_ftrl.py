#!/usr/bin/env python
# -*- coding: utf-8 -*-
#-------------------------------------------------------------------------------
# Copyright 2018 H2O.ai
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.
#-------------------------------------------------------------------------------
#
# Test FTRL modeling capabilities
#
#-------------------------------------------------------------------------------
import pickle
import datatable as dt
from datatable.models import Ftrl
from datatable.internal import frame_integrity_check
from datatable import f, stype, DatatableWarning
import pytest
import collections
import random
import math
from tests import assert_equals, noop



#-------------------------------------------------------------------------------
# Define namedtuple of test parameters, test model and accuracy
#-------------------------------------------------------------------------------
Params = collections.namedtuple("Params",["alpha", "beta", "lambda1", "lambda2",
                                          "nbins", "nepochs",
                                          "double_precision"])
tparams = Params(alpha = 1, beta = 2, lambda1 = 3, lambda2 = 4, nbins = 5,
                 nepochs = 6, double_precision = True)

tmodel = dt.Frame([[random.random() for _ in range(tparams.nbins)],
                   [random.random() for _ in range(tparams.nbins)]],
                   names=['z', 'n'])

default_params = Params(alpha = 0.005, beta = 1, lambda1 = 0, lambda2 = 1,
                        nbins = 1000000, nepochs = 1, double_precision = False)

epsilon = 0.01


#-------------------------------------------------------------------------------
# Test wrong parameter types, names and combination in constructor
#-------------------------------------------------------------------------------

def test_ftrl_construct_wrong_alpha_type():
    with pytest.raises(TypeError) as e:
        noop(Ftrl(alpha = "1.0"))
    assert ("Argument `alpha` in Ftrl() constructor should be a float, instead "
            "got <class 'str'>" == str(e.value))


def test_ftrl_construct_wrong_beta_type():
    with pytest.raises(TypeError) as e:
        noop(Ftrl(beta = "1.0"))
    assert ("Argument `beta` in Ftrl() constructor should be a float, instead "
            "got <class 'str'>" == str(e.value))


def test_ftrl_construct_wrong_lambda1_type():
    with pytest.raises(TypeError) as e:
        noop(Ftrl(lambda1 = "1.0"))
    assert ("Argument `lambda1` in Ftrl() constructor should be a float, "
            "instead got <class 'str'>" == str(e.value))


def test_ftrl_construct_wrong_lambda2_type():
    with pytest.raises(TypeError) as e:
        noop(Ftrl(lambda2 = "1.0"))
    assert ("Argument `lambda2` in Ftrl() constructor should be a float, "
            "instead got <class 'str'>" == str(e.value))


def test_ftrl_construct_wrong_nbins_type():
    with pytest.raises(TypeError) as e:
        noop(Ftrl(nbins = 1000000.0))
    assert ("Argument `nbins` in Ftrl() constructor should be an integer, "
            "instead got <class 'float'>" == str(e.value))


def test_ftrl_construct_wrong_nepochs_type():
    with pytest.raises(TypeError) as e:
        noop(Ftrl(nepochs = 10.0))
    assert ("Argument `nepochs` in Ftrl() constructor should be an integer, "
            "instead got <class 'float'>" == str(e.value))


def test_ftrl_construct_wrong_double_precision_type():
    with pytest.raises(TypeError) as e:
        noop(Ftrl(double_precision = 2))
    assert ("Argument `double_precision` in Ftrl() constructor should be a boolean, "
            "instead got <class 'int'>" == str(e.value))


def test_ftrl_construct_wrong_combination():
    with pytest.raises(TypeError) as e:
        noop(Ftrl(params=tparams, alpha = tparams.alpha))
    assert ("You can either pass all the parameters with `params` or any of "
            "the individual parameters with `alpha`, `beta`, `lambda1`, "
            "`lambda2`, `nbins`, `nepochs` or `double_precision` "
            "to Ftrl constructor, but not both at the same time" == str(e.value))


def test_ftrl_construct_unknown_arg():
    with pytest.raises(TypeError) as e:
        noop(Ftrl(c = 1.0))
    assert ("Ftrl() constructor got an unexpected keyword argument `c`" ==
            str(e.value))


def test_ftrl_construct_wrong_params_type():
    params = tparams._replace(alpha = "1.0")
    with pytest.raises(TypeError) as e:
        Ftrl(params)
    assert ("Expected a float, instead got <class 'str'>" == str(e.value))


def test_ftrl_construct_wrong_params_name():
    WrongParams = collections.namedtuple("WrongParams",["alpha", "lambda1"])
    wrong_params = WrongParams(alpha = 1, lambda1 = 0.01)
    with pytest.raises(AttributeError) as e:
        Ftrl(wrong_params)
    assert ("'WrongParams' object has no attribute 'beta'" == str(e.value))


#-------------------------------------------------------------------------------
# Test wrong parameter values in constructor
#-------------------------------------------------------------------------------

def test_ftrl_construct_wrong_alpha_value():
    with pytest.raises(ValueError) as e:
        noop(Ftrl(alpha = 0.0))
    assert ("Argument `alpha` in Ftrl() constructor should be positive: 0.0"
            == str(e.value))


def test_ftrl_construct_wrong_beta_value():
    with pytest.raises(ValueError) as e:
        noop(Ftrl(beta = -1.0))
    assert ("Argument `beta` in Ftrl() constructor should be greater than "
            "or equal to zero: -1.0" == str(e.value))


def test_ftrl_construct_wrong_lambda1_value():
    with pytest.raises(ValueError) as e:
        noop(Ftrl(lambda1 = -1.0))
    assert ("Argument `lambda1` in Ftrl() constructor should be greater than "
            "or equal to zero: -1.0" == str(e.value))


def test_ftrl_construct_wrong_lambda2_value():
    with pytest.raises(ValueError) as e:
        noop(Ftrl(lambda2 = -1.0))
    assert ("Argument `lambda2` in Ftrl() constructor should be greater than "
            "or equal to zero: -1.0" == str(e.value))


def test_ftrl_construct_wrong_nbins_value():
    with pytest.raises(ValueError) as e:
        noop(Ftrl(nbins = 0))
    assert ("Argument `nbins` in Ftrl() constructor should be positive: 0"
            == str(e.value))


def test_ftrl_construct_wrong_nepochs_value():
    with pytest.raises(ValueError) as e:
        noop(Ftrl(nepochs = -1))
    assert ("Argument `nepochs` in Ftrl() constructor cannot be negative: -1"
            == str(e.value))


#-------------------------------------------------------------------------------
# Test creation of Ftrl object
#-------------------------------------------------------------------------------

def test_ftrl_create_default():
    ft = Ftrl()
    assert ft.params == default_params


def test_ftrl_create_params():
    ft = Ftrl(tparams)
    assert ft.params == tparams


def test_ftrl_create_individual():
    ft = Ftrl(alpha = tparams.alpha, beta = tparams.beta,
                   lambda1 = tparams.lambda1, lambda2 = tparams.lambda2,
                   nbins = tparams.nbins, nepochs = tparams.nepochs,
                   double_precision = tparams.double_precision)
    assert ft.params == (tparams.alpha, tparams.beta,
                         tparams.lambda1, tparams.lambda2,
                         tparams.nbins, tparams.nepochs,
                         tparams.double_precision)


#-------------------------------------------------------------------------------
# Test getters and setters for valid FTRL parameters
#-------------------------------------------------------------------------------

def test_ftrl_get_parameters():
    ft = Ftrl(tparams)
    assert ft.params == tparams
    assert (ft.alpha, ft.beta, ft.lambda1, ft.lambda2, ft.nbins, ft.nepochs,
            ft.double_precision) == tparams


def test_ftrl_set_individual():
    ft = Ftrl(double_precision = tparams.double_precision)
    ft.alpha = tparams.alpha
    ft.beta = tparams.beta
    ft.lambda1 = tparams.lambda1
    ft.lambda2 = tparams.lambda2
    ft.nbins = tparams.nbins
    ft.nepochs = tparams.nepochs
    assert ft.params == tparams


#-------------------------------------------------------------------------------
# Test getters and setters for wrong types / names of FTRL parameters
#-------------------------------------------------------------------------------

def test_ftrl_set_wrong_alpha_type():
    ft = Ftrl()
    with pytest.raises(TypeError) as e:
        ft.alpha = "0.0"
    assert ("Expected a float, instead got <class 'str'>" == str(e.value))


def test_ftrl_set_wrong_beta_type():
    ft = Ftrl()
    with pytest.raises(TypeError) as e:
        ft.beta = "-1.0"
    assert ("Expected a float, instead got <class 'str'>" == str(e.value))


def test_ftrl_set_wrong_lambda1_type():
    ft = Ftrl()
    with pytest.raises(TypeError) as e:
        ft.lambda1 = "-1.0"
    assert ("Expected a float, instead got <class 'str'>" == str(e.value))


def test_ftrl_set_wrong_lambda2_type():
    ft = Ftrl()
    with pytest.raises(TypeError) as e:
        ft.lambda2 = "-1.0"
    assert ("Expected a float, instead got <class 'str'>" == str(e.value))


def test_ftrl_set_wrong_nbins_type():
    ft = Ftrl()
    with pytest.raises(TypeError) as e:
        ft.nbins = "0"
    assert ("Expected an integer, instead got <class 'str'>" == str(e.value))


def test_ftrl_set_wrong_nepochs_type():
    ft = Ftrl()
    with pytest.raises(TypeError) as e:
        ft.nepochs = "-10.0"
    assert ("Expected an integer, instead got <class 'str'>" == str(e.value))


def test_ftrl_set_wrong_interactions_type():
    ft = Ftrl()
    with pytest.raises(TypeError) as e:
        ft.interactions = True
    assert ("Expected an iterable, instead got <class 'bool'>"
            == str(e.value))


def test_ftrl_set_wrong_interactions_empty():
    ft = Ftrl()
    with pytest.raises(TypeError) as e:
        ft.interactions = [["C0"], []]
    assert ("Interaction lists cannot be empty" == str(e.value))


def test_ftrl_set_wrong_interactions_not_list():
    ft = Ftrl()
    with pytest.raises(TypeError) as e:
        ft.interactions = ["a", [1, 2]]
    assert ("Interactions should be a list of lists, instead "
            "encountered: 'a'" == str(e.value))


def test_ftrl_set_wrong_interactions_not_string():
    ft = Ftrl()
    with pytest.raises(TypeError) as e:
        ft.interactions = [["a", "b"], [1, 2]]
    assert ("Interaction features should be strings, instead "
            "encountered: 1" == str(e.value))


#-------------------------------------------------------------------------------
# Test getters and setters for wrong values of individual FTRL parameters
#-------------------------------------------------------------------------------

@pytest.mark.parametrize('value', [0.0, None, math.nan, math.inf])
def test_ftrl_set_bad_alpha_value(value):
    ft = Ftrl()
    with pytest.raises(ValueError) as e:
        ft.alpha = value
    assert ("Value should be positive: %s" % str(value) == str(e.value))


@pytest.mark.parametrize('value', [-1.0, None, math.nan, math.inf])
def test_ftrl_set_bad_beta_value(value):
    ft = Ftrl()
    with pytest.raises(ValueError) as e:
        ft.beta = value
    assert ("Value should be greater than or equal to zero: %s" % str(value)
            == str(e.value))


@pytest.mark.parametrize('value', [-1.0, None, math.nan, math.inf])
def test_ftrl_set_bad_lambda1_value(value):
    ft = Ftrl()
    with pytest.raises(ValueError) as e:
        ft.lambda1 = value
    assert ("Value should be greater than or equal to zero: %s" % str(value)
            == str(e.value))


@pytest.mark.parametrize('value', [-1.0, None, math.nan, math.inf])
def test_ftrl_set_bad_lambda2_value(value):
    ft = Ftrl()
    with pytest.raises(ValueError) as e:
        ft.lambda2 = value
    assert ("Value should be greater than or equal to zero: %s" % str(value)
            == str(e.value))


def test_ftrl_set_wrong_nbins_value():
    ft = Ftrl()
    with pytest.raises(ValueError) as e:
        ft.nbins = 0
    assert ("Value should be positive: 0" == str(e.value))


def test_ftrl_set_wrong_nepochs_value():
    ft = Ftrl()
    with pytest.raises(ValueError) as e:
        ft.nepochs = -10
    assert ("Integer value cannot be negative" == str(e.value))


#-------------------------------------------------------------------------------
# Test wrong training frame
#-------------------------------------------------------------------------------

def test_ftrl_fit_wrong_empty_training():
    ft = Ftrl()
    df_train = dt.Frame()
    df_target = dt.Frame([True])
    with pytest.raises(ValueError) as e:
        ft.fit(df_train, df_target)
    assert ("Training frame must have at least one column" ==
            str(e.value))


def test_ftrl_fit_wrong_empty_target():
    ft = Ftrl()
    df_train = dt.Frame([1.0, 2.0])
    df_target = dt.Frame()
    with pytest.raises(ValueError) as e:
        ft.fit(df_train, df_target)
    assert ("Target frame must have exactly one column" ==
            str(e.value))


def test_ftrl_fit_wrong_target_obj64():
    ft = Ftrl()
    df_train = dt.Frame(list(range(8)))
    df_target = dt.Frame([3, "point", None, None, 14, 15, 92, "6"])
    with pytest.raises(TypeError) as e:
        ft.fit(df_train, df_target)
    assert ("Targets of type `obj64` are not supported" ==
            str(e.value))


#-------------------------------------------------------------------------------
# Test hash function
#-------------------------------------------------------------------------------

def test_ftrl_colname_hashes():
    ncols = 10
    col_hashes_murmur2 = [ 1838936504594058908, 14027412581578625840,
                          14296604503264754754,  3956937694466614811,
                          10071734010655191393,  6063711047550005084,
                           4309007444360962581,  4517980897659475069,
                          17871586791652695964, 15779814813469047786]
    ft = Ftrl()
    df_train = dt.Frame([[0]] * ncols)
    df_target = dt.Frame([[True]])
    ft.fit(df_train, df_target)
    assert col_hashes_murmur2 == ft.colname_hashes


#-------------------------------------------------------------------------------
# Test wrong parameters for `fit` and `predict` methods
#-------------------------------------------------------------------------------

def test_ftrl_model_untrained():
    ft = Ftrl()
    assert ft.model == None


def test_ftrl_fit_no_frame():
    ft = Ftrl()
    with pytest.raises(ValueError) as e:
        ft.fit()
    assert ("Training frame parameter is missing"
            == str(e.value))


def test_ftrl_fit_no_target():
    ft = Ftrl()
    with pytest.raises(ValueError) as e:
        ft.fit(None)
    assert ("Target frame parameter is missing"
            == str(e.value))


def test_ftrl_fit_predict_nones():
    ft = Ftrl()
    ft.fit(None, None)
    df_target = ft.predict(None)
    assert df_target == None


def test_ftrl_predict_not_trained():
    ft = Ftrl()
    df_train = dt.Frame([[1, 2, 3], [True, False, True]])
    with pytest.raises(ValueError) as e:
        ft.predict(df_train)
    assert ("Cannot make any predictions, the model should be trained first"
            == str(e.value))


def test_ftrl_predict_wrong_frame():
    ft = Ftrl()
    df_train = dt.Frame([[1, 2, 3]])
    df_target = dt.Frame([[True, False, True]])
    df_predict = dt.Frame([[1, 2, 3], [4, 5, 6]])
    ft.fit(df_train, df_target)
    with pytest.raises(ValueError) as e:
        ft.predict(df_predict)
    assert ("Can only predict on a frame that has 1 column, i.e. has the "
            "same number of features as was used for model training"
            == str(e.value))


#-------------------------------------------------------------------------------
# Test `fit` and `predict` methods for binomial classification
#-------------------------------------------------------------------------------

def test_ftrl_fit_unique():
    ft = Ftrl(nbins = 10)
    df_train = dt.Frame(range(ft.nbins))
    df_target = dt.Frame([True] * ft.nbins)
    ft.fit(df_train, df_target)
    model = [[-0.5] * ft.nbins, [0.25] * ft.nbins]
    assert ft.model.to_list() == model


def test_ftrl_fit_unique_ignore_none():
    ft = Ftrl(nbins = 10)
    df_train = dt.Frame(range(2 * ft.nbins))
    df_target = dt.Frame([True] * ft.nbins + [None] * ft.nbins)
    ft.fit(df_train, df_target)
    model = [[-0.5] * ft.nbins, [0.25] * ft.nbins]
    assert ft.model.to_list() == model


def test_ftrl_fit_predict_bool():
    ft = Ftrl(alpha = 0.1, nepochs = 10000)
    df_train = dt.Frame([[True, False]])
    df_target = dt.Frame([[True, False]])
    ft.fit(df_train, df_target)
    df_target = ft.predict(df_train[:,0])
    assert df_target[0, 0] <= 1
    assert df_target[0, 0] >= 1 - epsilon
    assert df_target[1, 0] >= 0
    assert df_target[1, 0] < epsilon


def test_ftrl_fit_predict_int():
    ft = Ftrl(alpha = 0.1, nepochs = 10000)
    df_train = dt.Frame([[0, 1]])
    df_target = dt.Frame([[True, False]])
    ft.fit(df_train, df_target)
    df_target = ft.predict(df_train[:,0])
    assert df_target[0, 0] <= 1
    assert df_target[0, 0] >= 1 - epsilon
    assert df_target[1, 0] >= 0
    assert df_target[1, 0] < epsilon


def test_ftrl_fit_predict_float():
    ft = Ftrl(alpha = 0.1, nepochs = 10000)
    df_train = dt.Frame([[0.0, 1.0]])
    df_target = dt.Frame([[True, False]])
    ft.fit(df_train, df_target)
    df_target = ft.predict(df_train[:,0])
    assert df_target[0, 0] <= 1
    assert df_target[0, 0] >= 1 - epsilon
    assert df_target[1, 0] >= 0
    assert df_target[1, 0] < epsilon


def test_ftrl_fit_predict_string():
    ft = Ftrl(alpha = 0.1, nepochs = 10000)
    df_train = dt.Frame([["Monday", None, "", "Tuesday"]])
    df_target = dt.Frame([[True, False, False, True]])
    ft.fit(df_train, df_target)
    df_target = ft.predict(df_train[:,0])
    assert df_target[0, 0] <= 1
    assert df_target[0, 0] >= 1 - epsilon
    assert df_target[1, 0] >= 0
    assert df_target[1, 0] < epsilon


def test_ftrl_fit_predict_view():
    ft = Ftrl(nbins = 100)
    # Generate unique numbers, so that this test can be run in parallel
    df_train = dt.Frame(random.sample(range(ft.nbins), ft.nbins))
    df_target = dt.Frame([bool(random.getrandbits(1)) for _ in range(ft.nbins)])
    rows = range(ft.nbins//2, ft.nbins)

    # Train model and predict on a view
    ft.fit(df_train[rows,:], df_target[rows,:])
    predictions = ft.predict(df_train[rows,:])
    model = ft.model

    # Train model and predict on a frame
    ft.reset()
    df_train_range = df_train[rows,:]
    df_target_range = df_target[rows,:]
    df_train_range.materialize()
    df_target_range.materialize()
    ft.fit(df_train_range, df_target_range)
    predictions_range = ft.predict(df_train_range)

    assert_equals(model, ft.model)
    assert_equals(predictions, predictions_range)


@pytest.mark.parametrize('parameter, value',
                         [("nbins", 100),
                         ("interactions", [["C0", "C0"]])])
def test_ftrl_disable_setters_after_fit(parameter, value):
    ft = Ftrl(nbins = 10)
    df_train = dt.Frame(range(ft.nbins))
    df_target = dt.Frame([True] * ft.nbins)
    ft.fit(df_train, df_target)

    with pytest.raises(ValueError) as e:
        setattr(ft, parameter, value)
    assert ("Cannot change `"+parameter+"` for a trained model, "
            "reset this model or create a new one"
            == str(e.value))
    ft.reset()
    setattr(ft, parameter, value)


#-------------------------------------------------------------------------------
# Test multinomial regression
#-------------------------------------------------------------------------------

def test_ftrl_fit_predict_multinomial_vs_binomial():
    ft1 = Ftrl(nbins = 10, nepochs = 1)
    df_train1 = dt.Frame(range(ft1.nbins))
    df_target1 = dt.Frame({"target" : [True, False] * 5})
    ft1.fit(df_train1, df_target1)
    p1 = ft1.predict(df_train1)

    ft2 = Ftrl(nbins = 10, nepochs = 1)
    df_train2 = dt.Frame(range(ft2.nbins))
    df_target2 = dt.Frame(["target", None] * 5)
    ft2.fit(df_train2, df_target2)
    p2 = ft2.predict(df_train2)

    target_index = p2.colindex("target")
    multinomial_model = ft2.model[:, {
                                      "C0" : f[target_index * 2],
                                      "C1" : f[target_index * 2 + 1]
                                      }]
    assert_equals(ft1.model, multinomial_model)
    assert_equals(p1, p2[:, "target"])


def test_ftrl_fit_predict_multinomial():
    nepochs = 1000
    ft = Ftrl(alpha = 0.2, nepochs = nepochs, double_precision = True)
    labels = ("_negative", "red", "green", "blue")
    df_train = dt.Frame(["cucumber", None, "shift", "sky", "day", "orange",
                         "ocean"])
    df_target = dt.Frame(["green", "red", "red", "blue", "green", None,
                          "blue"])
    ft.fit(df_train, df_target)
    frame_integrity_check(ft.model)
    p = ft.predict(df_train)
    frame_integrity_check(p)
    p_dict = p.to_dict()
    p_list = p.to_list()
    sum_p =[sum(row) for row in zip(*p_list)]
    delta_sum = [abs(i - j) for i, j in zip(sum_p, [1] * 5)]
    delta_red =   [abs(i - j) for i, j in
                   zip(p_dict["red"], [0, 1, 1, 0, 0, 0.25, 0])]
    delta_green = [abs(i - j) for i, j in
                   zip(p_dict["green"], [1, 0, 0, 0, 1, 0.25, 0])]
    delta_blue =  [abs(i - j) for i, j in
                   zip(p_dict["blue"], [0, 0, 0, 1, 0, 0.25, 1])]

    assert max(delta_sum)   < 1e-12
    assert max(delta_red)   < epsilon
    assert max(delta_green) < epsilon
    assert max(delta_blue)  < epsilon
    assert collections.Counter(p.names) == collections.Counter(labels)


def test_ftrl_fit_predict_multinomial_online():
    ft = Ftrl(alpha = 0.2, nepochs = 1000, double_precision = True)
    labels = ("_negative", "red", "green", "blue")

    # Show only 1 label to the model
    df_train = dt.Frame(["cucumber"])
    df_target = dt.Frame(["green"])
    ft.fit(df_train, df_target)
    assert(ft.labels == ["_negative", "green"])
    assert(ft.model.shape == (ft.nbins, 4))

    # Show one more
    df_train = dt.Frame(["cucumber", None])
    df_target = dt.Frame(["green", "red"])
    ft.fit(df_train, df_target)
    assert(ft.labels == ["_negative", "green", "red"])
    assert(ft.model.shape == (ft.nbins, 6))

    # And one more
    df_train = dt.Frame(["cucumber", None, "shift", "sky", "day", "orange",
                         "ocean"])
    df_target = dt.Frame(["green", "red", "red", "blue", "green", None,
                          "blue"])
    ft.fit(df_train, df_target)
    assert(ft.labels == ["_negative", "green", "red", "blue"])
    assert(ft.model.shape == (ft.nbins, 8))

    # Do not add any new labels
    df_train = dt.Frame(["cucumber", None, "shift", "sky", "day", "orange",
                         "ocean"])
    df_target = dt.Frame(["green", "red", "red", "blue", "green", None,
                          "blue"])
    ft.fit(df_train, df_target)
    assert(ft.labels == ["_negative", "green", "red", "blue"])
    assert(ft.model.shape == (ft.nbins, 8))

    # Test predictions
    p = ft.predict(df_train)
    frame_integrity_check(p)
    p_dict = p.to_dict()
    p_list = p.to_list()
    sum_p =[sum(row) for row in zip(*p_list)]
    delta_sum = [abs(i - j) for i, j in zip(sum_p, [1] * 5)]
    delta_red =   [abs(i - j) for i, j in
                   zip(p_dict["red"], [0, 1, 1, 0, 0, 0.25, 0])]
    delta_green = [abs(i - j) for i, j in
                   zip(p_dict["green"], [1, 0, 0, 0, 1, 0.25, 0])]
    delta_blue =  [abs(i - j) for i, j in
                   zip(p_dict["blue"], [0, 0, 0, 1, 0, 0.25, 1])]

    assert max(delta_sum)   < 1e-12
    assert max(delta_red)   < epsilon
    assert max(delta_green) < epsilon
    assert max(delta_blue)  < epsilon
    assert collections.Counter(p.names) == collections.Counter(labels)



#-------------------------------------------------------------------------------
# Test early stopping
#-------------------------------------------------------------------------------

def test_ftrl_no_validation_set():
    nepochs = 1234
    nbins = 56
    ft = Ftrl(alpha = 0.5, nbins = nbins, nepochs = nepochs)
    r = range(ft.nbins)
    df_X = dt.Frame(r)
    df_y = dt.Frame(r)
    epoch_stopped = ft.fit(df_X, df_y)
    assert epoch_stopped == nepochs


def test_ftrl_no_early_stopping():
    nepochs = 1234
    nepochs_validation = 56
    nbins = 78
    ft = Ftrl(alpha = 0.5, nbins = nbins, nepochs = nepochs)
    r = range(ft.nbins)
    df_X = dt.Frame(r)
    df_y = dt.Frame(r)
    epoch_stopped = ft.fit(df_X, df_y, df_X, df_y,
                           nepochs_validation = nepochs_validation)
    assert epoch_stopped == nepochs


def test_ftrl_early_stopping_int():
    nepochs = 10000
    nepochs_validation = 5
    nbins = 10
    ft = Ftrl(alpha = 0.5, nbins = nbins, nepochs = nepochs)
    r = range(ft.nbins)
    df_X = dt.Frame(r)
    df_y = dt.Frame(r)
    epoch_stopped = ft.fit(df_X, df_y, df_X, df_y,
                           nepochs_validation = nepochs_validation)
    p = ft.predict(df_X)
    delta = [abs(i - j) for i, j in zip(p.to_list()[0], list(r))]
    assert epoch_stopped < nepochs
    assert int(epoch_stopped) % nepochs_validation == 0
    assert max(delta) < epsilon


def test_ftrl_early_stopping_float():
    nepochs = 10000
    nepochs_validation = 5.5
    nbins = 10
    ft = Ftrl(alpha = 0.5, nbins = nbins, nepochs = nepochs)
    r = range(ft.nbins)
    df_X = dt.Frame(r)
    df_y = dt.Frame(r)
    epoch_stopped = ft.fit(df_X, df_y, df_X, df_y,
                           nepochs_validation = nepochs_validation)
    p = ft.predict(df_X)
    delta = [abs(i - j) for i, j in zip(p.to_list()[0], list(r))]
    assert epoch_stopped < nepochs
    assert (epoch_stopped / nepochs_validation ==
            int(epoch_stopped / nepochs_validation))
    assert max(delta) < epsilon


def test_ftrl_early_stopping_regression():
    nepochs = 10000
    nepochs_validation = 5
    nbins = 10
    ft = Ftrl(alpha = 0.5, nbins = nbins, nepochs = nepochs)
    r = range(ft.nbins)
    df_X_train = dt.Frame(r)
    df_y_train = dt.Frame(r)
    df_X_validate = dt.Frame(range(-nbins, nbins))
    df_y_validate = df_X_validate
    epoch_stopped = ft.fit(df_X_train, df_y_train,
                           df_X_validate[nbins::,:], df_y_validate[nbins::,:],
                           nepochs_validation = nepochs_validation)
    p = ft.predict(df_X_train)
    delta = [abs(i - j) for i, j in zip(p.to_list()[0], list(r))]
    assert epoch_stopped < nepochs
    assert int(epoch_stopped) % nepochs_validation == 0
    assert max(delta) < epsilon


def test_ftrl_early_stopping_multinomial():
    nepochs = 2000
    ft = Ftrl(alpha = 0.2, nepochs = nepochs, double_precision = True)
    labels = ("_negative", "red", "green", "blue")
    df_train = dt.Frame(["cucumber", None, "shift", "sky", "day", "orange",
                         "ocean"])
    df_target = dt.Frame(["green", "red", "red", "blue", "green", None,
                          "blue"])
    epoch_stopped = ft.fit(df_train, df_target, df_train[:4, :], df_target[:4, :],
                           nepochs_validation = 1, validation_error = 1e-3)
    frame_integrity_check(ft.model)
    p = ft.predict(df_train)
    frame_integrity_check(p)
    p_dict = p.to_dict()
    p_list = p.to_list()
    sum_p =[sum(row) for row in zip(*p_list)]
    delta_sum = [abs(i - j) for i, j in zip(sum_p, [1] * 5)]
    delta_red =   [abs(i - j) for i, j in
                   zip(p_dict["red"], [0, 1, 1, 0, 0, 0.25, 0])]
    delta_green = [abs(i - j) for i, j in
                   zip(p_dict["green"], [1, 0, 0, 0, 1, 0.25, 0])]
    delta_blue =  [abs(i - j) for i, j in
                   zip(p_dict["blue"], [0, 0, 0, 1, 0, 0.25, 1])]

    assert epoch_stopped < nepochs
    assert max(delta_sum)   < 1e-6
    assert max(delta_red)   < epsilon
    assert max(delta_green) < epsilon
    assert max(delta_blue)  < epsilon
    assert collections.Counter(p.names) == collections.Counter(labels)


#-------------------------------------------------------------------------------
# Test regression for numerical targets
#-------------------------------------------------------------------------------

def test_ftrl_regression():
    ft = Ftrl(alpha = 2.0, nbins = 10, nepochs = 1000)
    r = range(ft.nbins)
    df_train = dt.Frame(r)
    df_target = dt.Frame(r)
    ft.fit(df_train, df_target)
    p = ft.predict(df_train)
    delta = [abs(i - j) for i, j in zip(p.to_list()[0], list(r))]
    assert max(delta) < epsilon


#-------------------------------------------------------------------------------
# Test feature importance
#-------------------------------------------------------------------------------

def test_ftrl_feature_importances():
    nrows = 10**4
    feature_names = ['unique', 'boolean', 'mod100']
    ft = Ftrl()
    df_train = dt.Frame([range(nrows),
                         [i % 2 for i in range(nrows)],
                         [i % 100 for i in range(nrows)]
                        ], names = feature_names)
    df_target = dt.Frame([False, True] * (nrows // 2))
    ft.fit(df_train, df_target)
    fi = ft.feature_importances
    assert fi.stypes == (stype.str32, stype.float32)
    assert fi.names == ("feature_name", "feature_importance")
    assert fi[:, 0].to_list() == [feature_names]
    assert fi[0, 1] < fi[2, 1]
    assert fi[2, 1] < fi[1, 1]


def test_ftrl_fi_shallowcopy():
    import copy
    ft = Ftrl(tparams)
    df_train = dt.Frame(random.sample(range(ft.nbins), ft.nbins))
    df_target = dt.Frame([bool(random.getrandbits(1)) for _ in range(ft.nbins)])
    ft.fit(df_train, df_target)
    fi1 = ft.feature_importances
    fi2 = copy.deepcopy(ft.feature_importances)
    ft.reset()
    assert ft.feature_importances == None
    assert_equals(fi1, fi2)


#-------------------------------------------------------------------------------
# Test feature interactions
#-------------------------------------------------------------------------------

def test_ftrl_interactions_wrong_features():
    nrows = 10**4
    feature_names = ['unique', 'boolean', 'mod100']
    feature_interactions = [["unique", "boolean"],["unique", "mod1000"]]
    ft = Ftrl()
    ft.interactions = feature_interactions
    df_train = dt.Frame([range(nrows),
                         [i % 2 for i in range(nrows)],
                         [i % 100 for i in range(nrows)]
                        ], names = feature_names)
    df_target = dt.Frame([False, True] * (nrows // 2))
    with pytest.raises(ValueError) as e:
        ft.fit(df_train, df_target)
    assert ("Feature 'mod1000' is used for interactions, however, it is "
            "missing in the training frame" == str(e.value))


def test_ftrl_interactions():
    nrows = 10**4
    feature_names = ['unique', 'boolean', 'mod100']
    feature_interactions = [["unique", "boolean"],
                            ["unique", "mod100"],
                            ["boolean", "mod100"],
                            ["boolean", "boolean", "boolean"]]
    interaction_names = ["unique:boolean", "unique:mod100",
                         "boolean:mod100", "boolean:boolean:boolean"]
    ft = Ftrl()
    ft.interactions = feature_interactions
    df_train = dt.Frame([range(nrows),
                         [i % 2 for i in range(nrows)],
                         [i % 100 for i in range(nrows)]
                        ], names = feature_names)
    df_target = dt.Frame([False, True] * (nrows // 2))
    ft.fit(df_train, df_target)
    fi = ft.feature_importances
    assert fi.stypes == (stype.str32, stype.float32)
    assert fi.names == ("feature_name", "feature_importance")
    assert fi[:, 0].to_list() == [feature_names + interaction_names]
    assert fi[0, 1] < fi[2, 1]
    assert fi[2, 1] < fi[1, 1]
    assert fi[3, 1] < fi[1, 1]
    assert fi[4, 1] < fi[1, 1]
    assert fi[5, 1] < fi[1, 1]
    # Make sure interaction of important features is still an important feature
    assert fi[0, 1] < fi[6, 1]
    assert fi[2, 1] < fi[6, 1]
    assert fi[3, 1] < fi[6, 1]
    assert fi[4, 1] < fi[6, 1]
    assert fi[5, 1] < fi[6, 1]


#-------------------------------------------------------------------------------
# Test pickling
#-------------------------------------------------------------------------------

def test_ftrl_pickling_empty_model():
    ft_pickled = pickle.dumps(Ftrl())
    ft_unpickled = pickle.loads(ft_pickled)
    assert ft_unpickled.model == None
    assert ft_unpickled.feature_importances == None
    assert ft_unpickled.params == Ftrl().params


def test_ftrl_reuse_pickled_empty_model():
    ft_pickled = pickle.dumps(Ftrl())
    ft_unpickled = pickle.loads(ft_pickled)
    ft_unpickled.nbins = 10
    df_train = dt.Frame({"id" : range(ft_unpickled.nbins)})
    df_target = dt.Frame([True] * ft_unpickled.nbins)
    ft_unpickled.fit(df_train, df_target)
    model = [[-0.5] * ft_unpickled.nbins, [0.25] * ft_unpickled.nbins]
    fi = dt.Frame([["id"], [0.0]])[:, [f[0], dt.float32(f[1])]]
    fi.names = ["feature_name", "feature_importance"]
    assert ft_unpickled.model.to_list() == model
    assert_equals(ft_unpickled.feature_importances, fi)


def test_ftrl_pickling_binomial():
    ft = Ftrl(nbins = 10)
    df_train = dt.Frame(range(ft.nbins), names = ["f1"])
    df_target = dt.Frame([True] * ft.nbins)
    ft.fit(df_train, df_target)
    ft_pickled = pickle.dumps(ft)
    ft_unpickled = pickle.loads(ft_pickled)
    frame_integrity_check(ft_unpickled.model)
    assert ft_unpickled.model.stypes == (stype.float32, stype.float32)
    assert_equals(ft.model, ft_unpickled.model)
    assert (ft_unpickled.feature_importances.names ==
            ('feature_name', 'feature_importance',))
    assert (ft_unpickled.feature_importances.stypes ==
            (stype.str32, stype.float32))
    assert_equals(ft.feature_importances, ft_unpickled.feature_importances)
    assert ft.params == ft_unpickled.params
    assert ft.labels == ft_unpickled.labels
    assert ft.colnames == ft_unpickled.colnames

    # Fit and predict
    ft_unpickled.fit(df_train, df_target)
    target_unpickled = ft_unpickled.predict(df_train)
    ft.fit(df_train, df_target)
    target = ft.predict(df_train)
    assert_equals(ft.model, ft_unpickled.model)
    assert_equals(target, target_unpickled)


def test_ftrl_pickling_multinomial():
    ft = Ftrl(alpha = 0.2, nbins = 100, nepochs = 1, double_precision = False)
    df_train = dt.Frame(["cucumber", None, "shift", "sky", "day", "orange",
                         "ocean"])
    df_target = dt.Frame(["green", "red", "red", "blue", "green", None,
                          "blue"])
    ft.interactions = [["C0", "C0"]]
    ft.fit(df_train, df_target)

    ft_pickled = pickle.dumps(ft)
    ft_unpickled = pickle.loads(ft_pickled)
    frame_integrity_check(ft_unpickled.model)
    assert ft_unpickled.model.stypes == (stype.float32,) * 8
    assert_equals(ft.model, ft_unpickled.model)
    assert (ft_unpickled.feature_importances.names ==
            ('feature_name', 'feature_importance',))
    assert (ft_unpickled.feature_importances.stypes ==
            (stype.str32, stype.float32))
    assert_equals(ft.feature_importances, ft_unpickled.feature_importances)
    assert ft.params == ft_unpickled.params
    assert ft.labels == ft_unpickled.labels
    assert ft.colnames == ft_unpickled.colnames
    assert ft.interactions == ft_unpickled.interactions

    # Fit and predict
    ft_unpickled.fit(df_train, df_target)
    target_unpickled = ft_unpickled.predict(df_train)
    ft.fit(df_train, df_target)
    target = ft.predict(df_train)
    assert_equals(ft.model, ft_unpickled.model)
    assert_equals(target, target_unpickled)

