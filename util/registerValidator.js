const api = require('./eosjs')().api
const tapos = { blocksBehind: 6, expireSeconds: 30 }

async function registerValidator(validator) {
  const authorization = [{actor:"boidcompower", permission:"active"}]
  const account = 'boidcompower'
  const actions = [
    // {authorization,account:"bo1ddactoken", name:'transfer',data: {from:"boidcompower", to:validator,quantity:"1.0000 BOID",memo:"Welcome to Boid Validators."}},
    {authorization,account, name:'regvalidator',data: {validator}},
    {authorization,account, name:'addvalprot',data: {validator, protocol_type:0,weight:100}},
    {authorization,account, name:'addvalprot',data: {validator, protocol_type:1,weight:100}}
  ]
  const result = await api.transact({actions},tapos).catch(el => console.error(el))
  if (result) console.log(result)
}

registerValidator('boidcompower').catch(el => console.log(el.toString()))
